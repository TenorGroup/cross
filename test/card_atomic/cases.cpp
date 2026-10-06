int failures = 0, checks = 0;
void check(bool ok, const std::string& name) { ++checks; if (!ok) { ++failures; std::cout << "FAIL " << name << '\n'; } }
std::string card(char payload = 'O') {
  const CardFileHead h{CARD_FILE_MAGIC, 7, 8, {0,0,2,2,0,2,2,2}, 8, 1, 450};
  return std::string(reinterpret_cast<const char*>(&h), sizeof(h)) + std::string(8, payload);
}
void prepare(HomeActivity& home) {
  home.coverBuffer = static_cast<uint8_t*>(malloc(8)); memset(home.coverBuffer, 'N', 8); home.coverBufferSize = 8;
  Storage = {}; Storage.files["card"] = card(); Storage.files["thumb"] = "thumb";
}
bool oldRecoverable() {
  return (Storage.exists("card") && Storage.files["card"] == card()) ||
         (Storage.exists("card.davbak") && Storage.files["card.davbak"] == card());
}
int main() {
  for (const auto* fault : {"open", "header", "payload", "sync", "close", "readclose", "stage", "backup", "promote"}) {
    HomeActivity h; prepare(h); Storage.fault = fault; h.saveCardFile();
    check(oldRecoverable(), std::string("preserve old on ") + fault);
    check(!h.cardOnCard, std::string("failed save state ") + fault);
  }
  for (const auto* cut : {"header", "payload", "sync", "close", "backup", "promote"}) {
    HomeActivity h; prepare(h); Storage.cut = cut;
    try { h.saveCardFile(); } catch (const PowerCut&) {}
    Storage.cut.clear();
    HomeActivity reader;
    const auto loaded = reader.loadCardFile("card", 7, 8, "thumb");
    check(loaded == HomeActivity::CardFile::Whole, std::string("read after cut ") + cut);
    check(reader.coverBuffer && (reader.coverBuffer[0] == 'O' || reader.coverBuffer[0] == 'N'), std::string("complete bytes after cut ") + cut);
  }
  {
    HomeActivity h; prepare(h); h.saveCardFile();
    check(h.cardOnCard && Storage.files["card"].size() == sizeof(CardFileHead) + 8 && Storage.files["card"].substr(sizeof(CardFileHead)) == std::string(8, 'N'), "successful full commit");
    check(!Storage.exists("card.davtmp") && !Storage.exists("card.davbak"), "clean commit");
    check(Storage.payloadWrites == 1, "single payload write");
  }
  {
    HomeActivity h; prepare(h); Storage.fault = "remove"; h.saveCardFile();
    check(h.cardOnCard && Storage.files["card"].size() == sizeof(CardFileHead) + 8 && Storage.files["card"].substr(sizeof(CardFileHead)) == std::string(8, 'N'), "commit with retained backup");
    check(Storage.files["card.davbak"] == card(), "backup retained on cleanup failure");
    Storage.fault.clear(); HomeActivity reader;
    check(reader.loadCardFile("card", 7, 8, "thumb") == HomeActivity::CardFile::Whole, "read committed main with backup");
    check(Storage.files["card.davbak"] == card(), "load retains backup until deferred save");
    reader.cardFilePending="card"; reader.saveCardFile();
    check(reader.cardOnCard && !Storage.exists("card.davbak"), "deferred save resolves retained backup");
  }
  for (int damage = 0; damage < 3; ++damage) {
    HomeActivity h; prepare(h); auto& raw = Storage.files["card"];
    if (damage == 0) raw[3] = '3';
    if (damage == 1) raw.pop_back();
    if (damage == 2) raw.push_back('X');
    check(h.loadCardFile("card", 7, 8, "thumb") == HomeActivity::CardFile::None, "invalid CRD4 rejected");
    Storage.files["card.davbak"] = card();
    check(h.loadCardFile("card", 7, 8, "thumb") == HomeActivity::CardFile::Whole, "valid backup served");
    check(Storage.files["card.davbak"] == card(), "backup retained beside invalid main");
  }
  {
    HomeActivity h; prepare(h); Storage.files.erase("card"); Storage.files["card.davtmp"] = card('N');
    check(h.loadCardFile("card",7,8,"thumb") == HomeActivity::CardFile::None, "uncommitted staging ignored");
    Storage.files["card.davbak"] = card();
    check(h.loadCardFile("card",7,8,"thumb") == HomeActivity::CardFile::Whole, "missing main recovered");
  }
  {
    HomeActivity h; prepare(h); check(h.loadCardFile("card",7,99,"thumb") == HomeActivity::CardFile::Cover, "stale text keeps cover");
    check(h.coverBufferSize == 4 && h.textRectH == 0, "cover-only size");
  }
  {
    HomeActivity h; prepare(h); Storage.files.erase("card"); Storage.files["card.davbak"] = card();
    Storage.fault = "backup";
    check(h.loadCardFile("card",7,8,"thumb") == HomeActivity::CardFile::Whole, "backup readable after restore failure");
  }
  {
    HomeActivity h; prepare(h); Storage.files["card.davbak"] = card(); Storage.failReadPath = "card";
    check(h.loadCardFile("card",7,8,"thumb") == HomeActivity::CardFile::Whole, "read fault serves backup");
    check(Storage.files["card"] == card() && Storage.files["card.davbak"] == card(), "read fault preserves both files");
  }
  {
    HomeActivity h; prepare(h); Storage.files["card.davbak"] = card(); Storage.fault="read";
    h.saveCardFile(); check(oldRecoverable() && !h.cardOnCard && Storage.writes == 0, "unresolved backup blocks replacement");
  }
  for (const bool coverChanged : {false, true}) {
    HomeActivity h; prepare(h); Storage.files["card.davbak"] = card();
    h.cardFileKey = 99;
    if (coverChanged) h.cardFileCoverKey = 99;
    h.loadCardFile("card", h.cardFileCoverKey, h.cardFileKey, "thumb");
    h.freeCoverBuffer(); h.coverBuffer = static_cast<uint8_t*>(malloc(8)); memset(h.coverBuffer,'N',8); h.coverBufferSize=8;
    h.coverBufferStored=true; h.textRectH=2; h.saveCardFile();
    HomeActivity reader;
    check(h.cardOnCard && reader.loadCardFile("card",h.cardFileCoverKey,99,"thumb") == HomeActivity::CardFile::Whole, "stale identity with retained backup refreshes");
    check(!Storage.exists("card.davbak"), "stale backup retired after refresh");
  }
  {
    HomeActivity h; prepare(h); Storage.files["card"][3]='3'; Storage.files["card.davbak"]=card();
    check(h.loadCardFile("card",7,8,"thumb") == HomeActivity::CardFile::Whole, "invalid main serves valid backup before save");
    h.cardFileKey=99; h.saveCardFile(); HomeActivity reader;
    check(h.cardOnCard && reader.loadCardFile("card",7,99,"thumb") == HomeActivity::CardFile::Whole, "invalid main restores backup then refreshes");
  }
  {
    HomeActivity h; prepare(h); Storage.files["card.davbak"]=card(); Storage.failReadPath="card";
    h.saveCardFile(); check(oldRecoverable() && Storage.files.count("card.davbak") && Storage.writes==0, "save read fault retains recovery copies");
  }
  {
    HomeActivity h; prepare(h); Storage.files["card"]="cut"; Storage.files["card.davbak"]=card();
    h.saveCardFile(); check(h.cardOnCard && Storage.files["card"].size()==sizeof(CardFileHead)+8, "truncated header restores backup before save");
  }
  std::cout << checks << " checks, " << failures << " failures\n"; return failures ? 1 : 0;
}
