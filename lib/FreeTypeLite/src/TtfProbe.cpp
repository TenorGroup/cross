#include "TtfProbe.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ft2build.h>

#include <cstdlib>
#include <cstring>
#include FT_FREETYPE_H
#include FT_MODULE_H

#ifndef SIMULATOR
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace ttfprobe {

const uint16_t BO_CHU[] = {
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D,
    0x002E, 0x002F, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B,
    0x003C, 0x003D, 0x003E, 0x003F, 0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049,
    0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F, 0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F, 0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065,
    0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F, 0x0070, 0x0071, 0x0072, 0x0073,
    0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x00C0, 0x00C1, 0x00C2,
    0x00C3, 0x00C8, 0x00C9, 0x00CA, 0x00CC, 0x00CD, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D9, 0x00DA, 0x00DD, 0x00E0,
    0x00E1, 0x00E2, 0x00E3, 0x00E8, 0x00E9, 0x00EA, 0x00EC, 0x00ED, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F9, 0x00FA,
    0x00FD, 0x0102, 0x0103, 0x0110, 0x0111, 0x0128, 0x0129, 0x0168, 0x0169, 0x01A0, 0x01A1, 0x01AF, 0x01B0, 0x1EA0,
    0x1EA1, 0x1EA2, 0x1EA3, 0x1EA4, 0x1EA5, 0x1EA6, 0x1EA7, 0x1EA8, 0x1EA9, 0x1EAA, 0x1EAB, 0x1EAC, 0x1EAD, 0x1EAE,
    0x1EAF, 0x1EB0, 0x1EB1, 0x1EB2, 0x1EB3, 0x1EB4, 0x1EB5, 0x1EB6, 0x1EB7, 0x1EB8, 0x1EB9, 0x1EBA, 0x1EBB, 0x1EBC,
    0x1EBD, 0x1EBE, 0x1EBF, 0x1EC0, 0x1EC1, 0x1EC2, 0x1EC3, 0x1EC4, 0x1EC5, 0x1EC6, 0x1EC7, 0x1EC8, 0x1EC9, 0x1ECA,
    0x1ECB, 0x1ECC, 0x1ECD, 0x1ECE, 0x1ECF, 0x1ED0, 0x1ED1, 0x1ED2, 0x1ED3, 0x1ED4, 0x1ED5, 0x1ED6, 0x1ED7, 0x1ED8,
    0x1ED9, 0x1EDA, 0x1EDB, 0x1EDC, 0x1EDD, 0x1EDE, 0x1EDF, 0x1EE0, 0x1EE1, 0x1EE2, 0x1EE3, 0x1EE4, 0x1EE5, 0x1EE6,
    0x1EE7, 0x1EE8, 0x1EE9, 0x1EEA, 0x1EEB, 0x1EEC, 0x1EED, 0x1EEE, 0x1EEF, 0x1EF0, 0x1EF1, 0x1EF2, 0x1EF3, 0x1EF4,
    0x1EF5, 0x1EF6, 0x1EF7, 0x1EF8, 0x1EF9};
const uint16_t BO_CHU_DAI = sizeof(BO_CHU) / sizeof(BO_CHU[0]);

namespace {

uint32_t heapConLai() {
#ifndef SIMULATOR
  return ESP.getFreeHeap();
#else
  return 0;
#endif
}

uint32_t bayGio() {
#ifndef SIMULATOR
  return millis();
#else
  return 0;
#endif
}

// --- do bo nho ------------------------------------------------------------------
//
// FreeType di qua FT_Memory cho MOI lan cap phat, nen gan bo dem vao day la dem duoc
// dung phan cua no, khong lan voi phan con lai cua may. Moi khoi mang them 8 byte dau de
// nho co cua chinh no; con so bao cao da tru phan do ra.

struct SoDoBoNho {
  uint32_t dangGiu = 0;
  uint32_t dinh = 0;
};

constexpr size_t MU = 8;  // giu co khoi, va giu cho con tro thang hang 8 byte

void* capPhat(FT_Memory memory, long size) {
  auto* so = static_cast<SoDoBoNho*>(memory->user);
  if (size <= 0) return nullptr;
  void* goc = malloc(static_cast<size_t>(size) + MU);
  if (!goc) return nullptr;
  *static_cast<uint32_t*>(goc) = static_cast<uint32_t>(size);
  so->dangGiu += static_cast<uint32_t>(size);
  if (so->dangGiu > so->dinh) so->dinh = so->dangGiu;
  return static_cast<char*>(goc) + MU;
}

void giaiPhong(FT_Memory memory, void* block) {
  if (!block) return;
  auto* so = static_cast<SoDoBoNho*>(memory->user);
  char* goc = static_cast<char*>(block) - MU;
  so->dangGiu -= *reinterpret_cast<uint32_t*>(goc);
  free(goc);
}

void* capLai(FT_Memory memory, long, long neu, void* block) {
  if (!block) return capPhat(memory, neu);
  if (neu <= 0) {
    giaiPhong(memory, block);
    return nullptr;
  }
  auto* so = static_cast<SoDoBoNho*>(memory->user);
  char* goc = static_cast<char*>(block) - MU;
  const uint32_t cu = *reinterpret_cast<uint32_t*>(goc);
  char* moi = static_cast<char*>(realloc(goc, static_cast<size_t>(neu) + MU));
  if (!moi) return nullptr;
  *reinterpret_cast<uint32_t*>(moi) = static_cast<uint32_t>(neu);
  so->dangGiu += static_cast<uint32_t>(neu) - cu;
  if (so->dangGiu > so->dinh) so->dinh = so->dangGiu;
  return moi + MU;
}

// --- doc font tu the -------------------------------------------------------------
//
// Font KHONG duoc nap ca tep vao bo nho: mot tep TTF thuong nang hon ca RAM cua chip.
// FreeType doc theo nhu cau qua FT_Stream, nen o day chi giu mot bo dem nho.
//
// Bo dem dang mot khoi: FreeType doc phan lon la tien len va gan nhau, nen mot khoi dung
// cho cat rat nhieu luot xuong the. Moi luot xuong the con phai qua khoa cua HalStorage,
// nen dem so luot la dem dung thu dat tien.

struct DongDoc {
  HalFile tep;
  uint8_t* dem = nullptr;
  uint32_t demRong = 0;
  uint32_t demTai = 0;  // vi tri trong tep cua byte dau trong bo dem
  uint32_t demCo = 0;   // so byte dang nam trong bo dem
  bool demCoHieuLuc = false;
  uint32_t soLanDoc = 0;
  uint32_t soByteDoc = 0;
};

unsigned long docThang(DongDoc& d, uint32_t offset, unsigned char* buffer, unsigned long count) {
  if (!d.tep.seek(offset)) return 0;
  // PHAI lap: mot lan read() cua the co the tra ve it hon so byte da hoi, va FreeType coi
  // mot lan doc thieu la loi doc font. Do 14/09/2026: thieu vong lap nay thi Bookerly
  // (450 KB) khong mo duoc trong khi Bokerlam (123 KB) van mo binh thuong.
  // Lap cho du so byte da hoi: mot lan read() cua the co the tra ve it hon, va FreeType
  // coi mot khung doc thieu la hong.
  //
  // LOI CON MO, do 14/09/2026: duong nay dung cho Bokerlam va IBM Plex Mono nhung KHONG
  // dung cho Bookerly (450 KB): font mo duoc ma bang anh xa ky tu rong, to 0/229 chu.
  // Da thu hai gia thuyet va CA HAI DEU SAI: (1) doc thieu byte, (2) mot lan doc qua lon.
  // Cat nho tung mieng 1 KB cho so lan doc 46 len 90 ma so byte va ket qua y nguyen.
  // Chua tim ra goc. Duong co bo dem o duoi dung cho ca ba font, nen dung duong do.
  unsigned long daTra = 0;
  while (daTra < count) {
    const int duoc = d.tep.read(buffer + daTra, count - daTra);
    d.soLanDoc++;
    if (duoc <= 0) break;
    d.soByteDoc += static_cast<uint32_t>(duoc);
    daTra += static_cast<unsigned long>(duoc);
  }
  return daTra;
}

unsigned long docQuaDem(FT_Stream stream, unsigned long offset, unsigned char* buffer, unsigned long count) {
  auto& d = *static_cast<DongDoc*>(stream->descriptor.pointer);
  if (count == 0) return 0;  // FreeType dung the nay de nhay vi tri, khong doi du lieu
  if (!buffer) return 0;

  if (d.demRong == 0) return docThang(d, offset, buffer, count);

  unsigned long daTra = 0;
  while (daTra < count) {
    const uint32_t can = static_cast<uint32_t>(offset + daTra);
    if (!d.demCoHieuLuc || can < d.demTai || can >= d.demTai + d.demCo) {
      // Khong nam trong khoi dang giu: keo mot khoi moi bat dau dung tai cho can.
      if (!d.tep.seek(can)) break;
      const int duoc = d.tep.read(d.dem, d.demRong);
      d.soLanDoc++;
      if (duoc <= 0) break;
      d.soByteDoc += static_cast<uint32_t>(duoc);
      d.demTai = can;
      d.demCo = static_cast<uint32_t>(duoc);
      d.demCoHieuLuc = true;
    }
    const uint32_t trongKhoi = d.demTai + d.demCo - can;
    const unsigned long lay = (count - daTra < trongKhoi) ? count - daTra : trongKhoi;
    memcpy(buffer + daTra, d.dem + (can - d.demTai), lay);
    daTra += lay;
  }
  return daTra;
}

void dongDong(FT_Stream) {}  // tep dong theo doi song cua DongDoc

}  // namespace

KetQua chay(const char* duongDanFont, const uint8_t pt, const uint16_t demRong) {
  KetQua kq;
  kq.heapTruoc = heapConLai();
  kq.heapThapNhat = kq.heapTruoc;
  const uint32_t t0 = bayGio();

  DongDoc dong;
  if (!Storage.openFileForRead("TTFP", duongDanFont, dong.tep)) {
    kq.loi = "khong mo duoc tep font tren the";
    return kq;
  }
  const size_t coTep = dong.tep.size();
  if (coTep == 0) {
    kq.loi = "tep font rong";
    return kq;
  }

  if (demRong > 0) {
    dong.dem = static_cast<uint8_t*>(malloc(demRong));
    if (!dong.dem) {
      kq.loi = "khong du bo nho cho bo dem doc";
      return kq;
    }
    dong.demRong = demRong;
  }

  SoDoBoNho so;
  FT_MemoryRec_ bonho{};
  bonho.user = &so;
  bonho.alloc = capPhat;
  bonho.free = giaiPhong;
  bonho.realloc = capLai;

  FT_Library thuVien = nullptr;
  if (FT_New_Library(&bonho, &thuVien) != 0) {
    free(dong.dem);
    kq.loi = "FT_New_Library hong";
    return kq;
  }
  FT_Add_Default_Modules(thuVien);

  FT_StreamRec_ luong{};
  luong.size = static_cast<unsigned long>(coTep);
  luong.pos = 0;
  luong.descriptor.pointer = &dong;
  luong.read = docQuaDem;
  luong.close = dongDong;

  FT_Open_Args args{};
  args.flags = FT_OPEN_STREAM;
  args.stream = &luong;

  const uint32_t tMo = bayGio();
  FT_Face mat = nullptr;
  if (FT_Open_Face(thuVien, &args, 0, &mat) != 0) {
    FT_Done_Library(thuVien);
    free(dong.dem);
    kq.loi = "FT_Open_Face tu choi tep nay";
    return kq;
  }
  kq.msMoFont = bayGio() - tMo;

  // 150 DPI: dung con so converter tren may tinh dang dung, de so sanh duoc.
  const uint32_t tCo = bayGio();
  if (FT_Set_Char_Size(mat, pt * 64, pt * 64, 150, 150) != 0) {
    FT_Done_Face(mat);
    FT_Done_Library(thuVien);
    free(dong.dem);
    kq.loi = "FT_Set_Char_Size hong";
    return kq;
  }
  kq.msDatCo = bayGio() - tCo;

  const uint32_t tTo = bayGio();
  for (uint16_t i = 0; i < BO_CHU_DAI; i++) {
    const uint32_t diemMa = BO_CHU[i];
    if (FT_Get_Char_Index(mat, diemMa) == 0) {
      kq.soChuThieu++;
      continue;
    }
    if (FT_Load_Char(mat, diemMa, FT_LOAD_RENDER) != 0) {
      kq.soChuThieu++;
      continue;
    }
    kq.soChuToDuoc++;
    kq.tongDiemAnh += static_cast<uint32_t>(mat->glyph->bitmap.width) * mat->glyph->bitmap.rows;
    const uint32_t heap = heapConLai();
    if (heap < kq.heapThapNhat) kq.heapThapNhat = heap;
#ifndef SIMULATOR
    // Task nay chay o cung muc uu tien voi luong chinh tren mot loi duy nhat. Khong nhuong
    // thi luong ranh khong bao gio chay va watchdog cua no se keu.
    if ((i & 63) == 63) {
      kq.soLanNhuong++;
      vTaskDelay(1);
    }
#endif
  }
  kq.msToChu = bayGio() - tTo;

  kq.dinhBoNhoFt = so.dinh;

  FT_Done_Face(mat);
  FT_Done_Library(thuVien);
  free(dong.dem);

  kq.soLanDocThe = dong.soLanDoc;
  kq.soByteDocThe = dong.soByteDoc;
  kq.heapSau = heapConLai();
  kq.msTong = bayGio() - t0;
  kq.moDuoc = true;

  if (so.dangGiu != 0) {
    LOG_ERR("TTFP", "FreeType con giu %u byte sau khi dong, co ro ri", static_cast<unsigned>(so.dangGiu));
  }
  return kq;
}

KetQua chayTrenTaskRieng(const char* duongDanFont, const uint8_t pt, const uint16_t demRong,
                         const uint32_t nganXepByte) {
#ifdef SIMULATOR
  (void)nganXepByte;
  return chay(duongDanFont, pt, demRong);
#else
  struct Goi {
    const char* duongDan;
    uint8_t pt;
    uint16_t demRong;
    KetQua kq;
    volatile bool xong;
  } goi{duongDanFont, pt, demRong, {}, false};

  auto than = [](void* thamSo) {
    auto* g = static_cast<Goi*>(thamSo);
    g->kq = chay(g->duongDan, g->pt, g->demRong);
    // Doc luc con trong task: sau khi task chet thi khong doc duoc nua.
    g->kq.nganXepConDu = uxTaskGetStackHighWaterMark(nullptr);
    g->xong = true;
    vTaskDelete(nullptr);
  };

  TaskHandle_t tay = nullptr;
  if (xTaskCreate(than, "TtfProbe", nganXepByte, &goi, 1, &tay) != pdPASS) {
    KetQua kq;
    kq.loi = "khong tao duoc task cho phep do";
    return kq;
  }

  // `goi` nam tren ngan xep cua luong goi, nen luong goi PHAI doi cho xong moi duoc tra ve.
  while (!goi.xong) {
    if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  goi.kq.nganXepCap = nganXepByte;
  return goi.kq;
#endif
}

}  // namespace ttfprobe
