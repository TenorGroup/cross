#pragma once

#include <cstdint>

// Radio BLE cua page turner khong co moc tu tat nao: bat len la no chay toi khi
// nguoi dung tat tuy chon, the nho bi chiem, hoac doi man. Mot may de trong tui
// vi the giu radio song mai, va vong tiet kiem dien trong main.cpp co chu y giu
// CPU o toc do day khi radio con chay (chu thich 18/09/2026: ha xung luc radio
// song tung gay loi HCI ack roi watchdog reset tren X3). Hai dieu do cong lai la
// may nam im ma van an pin.
//
// Luat o day tach rieng de bai kiem host bom duoc thoi gian gia, khong phai cho
// het nam phut that.
namespace bleidle {

// Chua ai noi trong bay nhieu lau thi ha radio xuong.
constexpr uint32_t kIdleOffMs = 5u * 60u * 1000u;

// `idleMs` la khoang tu lan CUOI CUNG con mot thiet bi dang noi, hoac tu luc bat
// radio neu chua ai noi lan nao. Dang noi thi khong bao gio tat, vi dieu khien lat
// trang im rat lau giua hai lan bam va nguoi dung van coi la dang dung.
constexpr bool shouldStop(const bool running, const bool connected, const uint32_t idleMs,
                          const uint32_t limitMs = kIdleOffMs) {
  return running && !connected && idleMs >= limitMs;
}

}  // namespace bleidle
