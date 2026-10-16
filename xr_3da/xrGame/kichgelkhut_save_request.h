#pragma once

// Reuse the existing single-player request byte; its low bit keeps update_name.
namespace kichgelkhut_save
{
enum RequestFlags
{
    update_name = 1,
    automatic = 2,
    notify = 4
};
}
