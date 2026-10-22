#pragma once

class FlashPlayer;

// FSCommand "sbdd" (FUN_0099c490 / the base flash handler): the movie's
// SetBDD() sets db_<category> = 1 for the string categories it uses; the
// game fills the /:<_flash> variable of every DBLoc row of those
// categories with its localized string (db_data.cpr DBLoc, db_<lang>.cpr).
// [INF from the names: the handler body was not followed.]
void ApplyFlashLocalization( FlashPlayer &player );
