#pragma once
// Real translation rows used by the production stats renderer fixture.
enum StrId { STR_SLEEP_STATS, STR_STATS_BOOK_TIME, STR_STATS_CHART_NOTE, STR_STATS_DAYS, STR_STATS_HOURS, STR_STATS_MEAN_WEEK, STR_STATS_MINUTES, STR_STATS_MONTH, STR_STATS_NIGHT_28, STR_STATS_POSITION, STR_STATS_READ_ERROR, STR_STATS_SESSIONS_28, STR_STATS_SHORT_LONG, STR_STATS_STREAK, STR_STATS_TODAY, STR_STATS_UNDATED, STR_STATS_UNDER_MINUTE, STR_STATS_WEEK };
inline int statsLocale = 0;
inline const char* tr(StrId id) {
 static const char* rows[3][18] = {
{"Thống kê đọc", "Thời gian cuốn này", "7 ngày gần đây. Dấu chấm: Chưa ghi nhận.", "ngày", "giờ", "TB/ngày đọc (7 ngày)", "phút", "30 ngày qua", "Đọc đêm (28 ngày)", "Vị trí đang mở", "Không đọc được thống kê", "Phiên (28 ngày)", "Phiên ngắn / dài", "Chuỗi ngày đọc", "Hôm nay", "Chưa có ngày", "Dưới 1 phút", "7 ngày qua"},
{"Reading statistics", "Time with this book", "Last 7 days. Dot: Not recorded.", "days", "h", "Avg/read day (7 days)", "min", "Last 30 days", "Night (28 days)", "Current position", "Cannot read statistics", "Sessions (28 days)", "Short / long sessions", "Reading streak", "Today", "Without a date", "Under 1 min", "Last 7 days"},
{"阅读统计", "本书打开时间", "近 7 天。圆点：未记录。", "天", "小时", "阅读日均（7 天）", "分钟", "近 30 天", "夜读（28 天）", "当前位置", "无法读取统计", "阅读次数（28 天）", "短时/长时阅读", "连续阅读", "今天", "日期未记录", "不足 1 分钟", "近 7 天"},
 };
 return rows[statsLocale][id];
}
