#include "core/rle.h"

#include <cctype>
#include <cstdio>
#include <string>

#include "core/kernel.h"
#include "util/error.h"

namespace ecosim {
namespace rle {

namespace {

constexpr int32_t kMaxRun = 1 << 24;  // beyond this the text is corrupt, not large

std::string rule_name(const RuleTable& rules) {
  std::string b = "B";
  std::string s = "S";
  for (int32_t i = 0; i < 9; ++i) {
    if (rules.birth[i]) b += static_cast<char>('0' + i);
    if (rules.survive[i]) s += static_cast<char>('0' + i);
  }
  if (b.size() == 1 && s.size() == 1) return {};  // nothing to declare
  return b + "/" + s;
}

void append_run(std::string& out, int32_t count, char letter) {
  if (count <= 0) return;
  if (count > 1) out += std::to_string(count);
  out += letter;
}

}  // namespace

int32_t worst_case_size(int32_t w, int32_t h) {
  const int64_t cells = static_cast<int64_t>(w) * h;
  const int64_t bytes = 96 + cells * 2 + static_cast<int64_t>(h) * 10 + 8;
  return static_cast<int32_t>(bytes > 0x7fffffff ? 0x7fffffff : bytes);
}

int32_t encode(const Planes& planes, int32_t w, int32_t h, const RuleTable& rules,
               std::string& out) {
  int32_t x0 = w, x1 = -1, y0 = h, y1 = -1;
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* row = planes.species.data() + static_cast<size_t>(y) * w;
    for (int32_t x = 0; x < w; ++x) {
      if (row[x] == 0) continue;
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (y < y0) y0 = y;
      if (y > y1) y1 = y;
    }
  }

  if (x1 < x0) {  // empty world: a 1x1 box of dead cells is the convention
    x0 = 0;
    x1 = 0;
    y0 = 0;
    y1 = 0;
  }

  const int32_t bw = x1 - x0 + 1;
  const int32_t bh = y1 - y0 + 1;

  char header[128];
  const std::string rule = rule_name(rules);
  if (rule.empty()) {
    std::snprintf(header, sizeof(header), "x = %d, y = %d\n", bw, bh);
  } else {
    std::snprintf(header, sizeof(header), "x = %d, y = %d, rule = %s\n", bw, bh, rule.c_str());
  }

  out.clear();
  out += header;

  for (int32_t y = y0; y <= y1; ++y) {
    const uint8_t* row = planes.species.data() + static_cast<size_t>(y) * w;
    int32_t run = 0;
    char run_letter = 'b';
    for (int32_t x = x0; x <= x1; ++x) {
      const char letter = row[x] != 0 ? 'o' : 'b';
      if (letter != run_letter && run > 0) {
        append_run(out, run, run_letter);
        run = 0;
      }
      run_letter = letter;
      ++run;
    }
    append_run(out, run, run_letter);
    out += (y < y1) ? "$\n" : "!\n";
  }

  return ECOSIM_OK;
}

int32_t decode(std::string_view text, Planes& planes, int32_t w, int32_t h, int32_t origin_x,
               int32_t origin_y, int32_t& out_w, int32_t& out_h, std::string& warning) {
  out_w = 0;
  out_h = 0;
  warning.clear();

  if (text.empty()) {
    set_error("RLE 文本为空");
    return ECOSIM_ERR_INVALID;
  }

  // The optional header "x = A, y = B[, rule = ...]" is informational: the
  // world keeps its own size (appendix A.4.3), so only the body matters.
  auto skip_line = [&text](size_t from) {
    size_t j = from;
    while (j < text.size() && text[j] != '\n') ++j;
    return j < text.size() ? j + 1 : j;
  };
  auto first_nonspace = [&text](size_t from) {
    size_t j = from;
    while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
    return j;
  };

  size_t i = first_nonspace(0);
  while (i < text.size() && text[i] == '#') i = first_nonspace(skip_line(i));
  if (i < text.size() && (text[i] == 'x' || text[i] == 'X')) i = skip_line(i);

  int32_t x = 0;
  int32_t y = 0;
  int64_t count = 0;
  bool has_count = false;
  int32_t clipped_cols = 0;
  int32_t clipped_rows = 0;
  bool done = false;

  auto place = [&](int32_t rel_x, int32_t rel_y, bool alive) {
    if (rel_x >= out_w) out_w = rel_x + 1;
    if (rel_y >= out_h) out_h = rel_y + 1;
    const int32_t px = origin_x + rel_x;
    const int32_t py = origin_y + rel_y;
    if (px < 0 || py < 0 || px >= w || py >= h) {
      if (px >= w) ++clipped_cols;
      if (py >= h) ++clipped_rows;
      return;
    }
    planes.species[static_cast<size_t>(py) * w + px] = alive ? 1 : 0;
    if (!alive) return;
    planes.age[static_cast<size_t>(py) * w + px] = 0;
  };

  for (; i < text.size() && !done; ++i) {
    const char c = static_cast<char>(text[i]);
    if (c >= '0' && c <= '9') {
      count = count * 10 + (c - '0');
      has_count = true;
      if (count > kMaxRun) {
        set_error("RLE 游程长度超出合理范围");
        return ECOSIM_ERR_INVALID;
      }
      continue;
    }
    if (c == '#') {
      i = skip_line(i) - 1;  // the loop will ++i
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;

    const int32_t n = has_count ? static_cast<int32_t>(count) : 1;
    count = 0;
    has_count = false;

    if (c == 'b' || c == 'o') {
      const bool alive = (c == 'o');
      for (int32_t k = 0; k < n; ++k) place(x++, y, alive);
      continue;
    }
    if (c == '$') {
      // Row separator only: writing anything here would clobber the column 0
      // cell of the row just finished.
      y += n;
      x = 0;
      continue;
    }
    if (c == '!') {
      done = true;
      continue;
    }

    char msg[96];
    std::snprintf(msg, sizeof(msg), "RLE 中出现无法识别的字符 '%c'", c);
    set_error(msg);
    return ECOSIM_ERR_INVALID;
  }

  if (!done && (x > 0 || y > 0)) {
    set_error("RLE 缺少结束符 '!'");
    return ECOSIM_ERR_INVALID;
  }

  if (clipped_cols > 0 || clipped_rows > 0) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "图案超出网格，已裁剪 %d 列 %d 行", clipped_cols,
                  clipped_rows);
    warning = msg;
    set_error(msg);  // a warning the caller may show; the call still succeeds
  }

  return ECOSIM_OK;
}

}  // namespace rle
}  // namespace ecosim
