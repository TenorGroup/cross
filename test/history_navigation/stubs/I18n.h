#pragma once
enum {STR_STATS_BY_BOOK,STR_QUOTES_PREVIOUS,STR_QUOTES_NEXT,STR_STATS_NOT_RECORDED,STR_STATS_READ_ERROR,STR_HOME_TAB_STATS};
inline const char* tr(int id){static const char* labels[]={"Books","Previous","Next","No stats","Read error","Stats"};return labels[id];}
