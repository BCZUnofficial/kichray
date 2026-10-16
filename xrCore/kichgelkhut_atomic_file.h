#pragma once

// Included by LocatorAPI.cpp. All temporary files stay on the destination volume.
namespace kichgelkhut_atomic_file
{
static volatile LONG sequence = 0;

static bool write_all(HANDLE file, const void* buffer, u32 size)
{
    const unsigned char* cursor = static_cast<const unsigned char*>(buffer);
    while (size)
    {
        const DWORD requested = size > 0x1000000u ? 0x1000000u : size;
        DWORD written = 0;
        if (!WriteFile(file, cursor, requested, &written, NULL)) return false;
        if (!written || written > requested)
        {
            SetLastError(ERROR_WRITE_FAULT);
            return false;
        }
        cursor += written;
        size -= written;
    }
    return true;
}

static bool write(const char* filename, const void* prefix, u32 prefix_size,
    const void* data, u32 data_size)
{
    if (!filename || !filename[0] || strlen(filename) + 64 >= MAX_PATH ||
        (prefix_size && !prefix) || (data_size && !data) ||
        data_size > 0xffffffffu - prefix_size)
    {
        Msg("! [SAVE] Invalid path, buffers or file size for atomic write");
        return false;
    }
    char temporary[MAX_PATH];
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 16; ++attempt)
    {
        sprintf_s(temporary, "%s.kich_tmp_%lu_%ld", filename,
            GetCurrentProcessId(), InterlockedIncrement(&sequence));
        file = CreateFileA(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
        {
            Msg("! [SAVE] Cannot create temporary file for '%s' (Windows error %lu)", filename, error);
            return false;
        }
    }
    if (file == INVALID_HANDLE_VALUE)
    {
        Msg("! [SAVE] Temporary file name collision for '%s'", filename);
        return false;
    }

    const char* stage = "write";
    bool ok = write_all(file, prefix, prefix_size) && write_all(file, data, data_size);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (ok)
    {
        stage = "flush";
        ok = !!FlushFileBuffers(file);
        if (!ok) error = GetLastError();
    }
    if (!CloseHandle(file) && ok)
    {
        stage = "close";
        error = GetLastError();
        ok = false;
    }
    if (ok)
    {
        stage = "replace";
        ok = !!MoveFileExA(temporary, filename, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) error = GetLastError();
    }
    if (!ok)
    {
        Msg("! [SAVE] %s failed for '%s' (Windows error %lu); replacement not completed",
            stage, filename, error);
        if (!DeleteFileA(temporary))
            Msg("! [SAVE] Cannot remove temporary file '%s' (Windows error %lu)", temporary, GetLastError());
    }
    return ok;
}
}
