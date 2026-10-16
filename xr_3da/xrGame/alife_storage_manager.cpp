////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_storage_manager.cpp
//	Created 	: 25.12.2002
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife Simulator storage manager
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "alife_storage_manager.h"
#include "alife_simulator_header.h"
#include "alife_time_manager.h"
#include "alife_spawn_registry.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
#include "alife_group_registry.h"
#include "alife_registry_container.h"
#include "xrserver.h"
#include "level.h"
#include "../x_ray.h"
#include "saved_game_wrapper.h"
#include "string_table.h"
#include "../igame_persistent.h"
#include "kichgelkhut_save_request.h"
#include "HUDManager.h"
#include "UIGameCustom.h"
#include "ui/UIStatic.h"
#include "Actor.h"
#include "MainMenu.h"

using namespace ALife;

extern string_path g_last_saved_game;

CALifeStorageManager::~CALifeStorageManager()
{
}

bool CALifeStorageManager::save(LPCSTR save_name, bool update_name)
{
    string_path candidate;
    if (save_name)
    {
        if (!save_name[0] || strlen(save_name) + strlen(SAVE_EXTENSION) >= sizeof(candidate))
        {
            Msg("! [SAVE] Empty or excessively long save name");
            return false;
        }
        strconcat(sizeof(candidate), candidate, save_name, SAVE_EXTENSION);
    }
    else
    {
        if (!m_save_name[0])
        {
            Msg("! [SAVE] No save name specified");
            return false;
        }
        strcpy_s(candidate, m_save_name);
    }
    // Do not allow a save slot name to escape the save directory.
    for (const unsigned char* c = reinterpret_cast<const unsigned char*>(candidate); *c; ++c)
        if (*c < 32 || strchr("/\\:*?\"<>|", *c))
        {
            Msg("! [SAVE] Invalid character in save name: %s", candidate);
            return false;
        }

    u32 source_count;
    u32 dest_count;
    void* dest_data;
    {
        CMemoryWriter stream;
        header().save(stream);
        time_manager().save(stream);
        spawns().save(stream);
        objects().save(stream);
        registry().save(stream);
        source_count = stream.tell();
        dest_count = rtc_csize(source_count);
        dest_data = xr_malloc(dest_count);
        dest_count = rtc_compress(dest_data, dest_count, stream.pointer(), source_count);
    }

    string_path filename;
    const size_t root_size = strlen(FS.get_path("$game_saves$")->m_Path);
    if (root_size + strlen(candidate) + 1 >= sizeof(filename))
    {
        Msg("! [SAVE] Save path is too long");
        xr_free(dest_data);
        return false;
    }
    FS.update_path(filename, "$game_saves$", candidate);
    // Identical .sav bytes: marker, ALife version, decompressed length, compressed data.
    const u32 prefix[3] = {u32(-1), ALIFE_VERSION, source_count};
    const bool saved = FS.file_write_atomic(filename, prefix, sizeof(prefix), dest_data, dest_count);
    xr_free(dest_data);
    if (!saved) return false;

    if (update_name) strcpy_s(m_save_name, candidate);
    strcpy_s(g_last_saved_game, candidate);
    const size_t length = strlen(g_last_saved_game);
    const size_t extension_length = strlen(SAVE_EXTENSION);
    if (length >= extension_length)
        g_last_saved_game[length - extension_length] = 0;
    Msg("* Game %s is successfully saved to file '%s'", candidate, filename);
    return true;
}

void CALifeStorageManager::load(void* buffer, const u32& buffer_size, LPCSTR file_name)
{
	IReader source(buffer, buffer_size);
	header().load(source);
	time_manager().load(source);
	spawns().load(source, file_name);

#ifdef PRIQUEL
	graph().on_load();
#endif // PRIQUEL

	objects().load(source);

	VERIFY(can_register_objects());
	can_register_objects(false);
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator B = objects().objects().begin();
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator E = objects().objects().end();
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator I;
	for (I = B; I != E; ++I)
	{
		ALife::_OBJECT_ID id = (*I).second->ID;
		(*I).second->ID = server().PerformIDgen(id);
		VERIFY(id == (*I).second->ID);
		register_object((*I).second, false);
	}

	registry().load(source);

	can_register_objects(true);

	for (I = B; I != E; ++I)
		(*I).second->on_register();
}

bool CALifeStorageManager::load(LPCSTR save_name)
{
	CTimer timer;
	timer.Start();
	string256 save;
	strcpy(save, m_save_name);
	if (!save_name)
	{
		if (!xr_strlen(m_save_name))
			R_ASSERT2(false, "There is no file name specified!");
	}
	else
		strconcat(sizeof(m_save_name), m_save_name, save_name, SAVE_EXTENSION);
	string_path file_name;
	FS.update_path(file_name, "$game_saves$", m_save_name);

	IReader* stream;
	stream = FS.r_open(file_name);
	if (!stream)
	{
		Msg("* Cannot find saved game %s", file_name);
		strcpy(m_save_name, save);
		return (false);
	}

	CHECK_OR_EXIT(CSavedGameWrapper::valid_saved_game(*stream),
				  make_string("%s\nSaved game version mismatch or saved game is corrupted", file_name));

	string512 temp;
	strconcat(sizeof(temp), temp, CStringTable().translate("st_loading_saved_game").c_str(), " \"", save_name,
			  SAVE_EXTENSION, "\"");
	g_pGamePersistent->LoadTitle(temp);

	unload();
	reload(m_section);

	u32 source_count = stream->r_u32();
	void* source_data = xr_malloc(source_count);
	rtc_decompress(source_data, source_count, stream->pointer(), stream->length() - 3 * sizeof(u32));
	FS.r_close(stream);
	load(source_data, source_count, file_name);
	xr_free(source_data);

	groups().on_after_game_load();

	VERIFY(graph().actor());

	Msg("* Game %s is successfully loaded from file '%s' (%.3fs)", save_name, file_name, timer.GetElapsed_sec());

	return (true);
}

static void show_save_result(LPCSTR name, u8 flags, bool saved)
{
    if (!(flags & kichgelkhut_save::notify) || g_dedicated_server || !g_pGameLevel || !g_actor)
        return;
    if (!HUD().GetUI() || !HUD().GetUI()->UIGame()) return;
    const bool automatic = !!(flags & kichgelkhut_save::automatic);
    SDrawStaticStruct* message = HUD().GetUI()->UIGame()->AddCustomStatic(
        saved && automatic ? "autosave" : "game_saved", true);
    if (message)
    {
        message->m_endTime = Device.fTimeGlobal + 3.f;
        if (!saved || !automatic)
        {
            string512 text;
            sprintf_s(text, "%s: %s", saved ? CStringTable().translate("st_game_saved").c_str() : "Save failed", name);
            message->wnd()->SetText(text);
        }
    }
    if (saved && MainMenu())
    {
        string_path preview, relative;
        if (strlen(name) + 5 >= sizeof(relative)) return;
        strconcat(sizeof(relative), relative, name, ".dds");
        if (strlen(FS.get_path("$game_saves$")->m_Path) + strlen(relative) + 1 >= sizeof(preview)) return;
        FS.update_path(preview, "$game_saves$", relative);
        MainMenu()->Screenshot(IRender_interface::SM_FOR_GAMESAVE, preview);
    }
}

bool CALifeStorageManager::save(NET_Packet& net_packet)
{
    shared_str game_name;
    net_packet.r_stringZ(game_name);
    const u8 flags = net_packet.r_u8();
    prepare_objects_for_save();
    const bool saved = save(*game_name, !!(flags & kichgelkhut_save::update_name));
    show_save_result(game_name.c_str(), flags, saved);
    return saved;
}

void CALifeStorageManager::prepare_objects_for_save()
{
	Level().ClientSend();
	Level().ClientSave();
}
