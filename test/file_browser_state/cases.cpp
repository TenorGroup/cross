int main() {
  using filebrowser::Store;
  using filebrowser::SortMode;
  constexpr char path[] = "/.crosspoint/file-browser.json";
  {
    Storage = {};
    Store missing;
    assert(missing.get().crossPath == "/" && missing.get().uglyPath == "/");
    assert(missing.get().crossSort == SortMode::Name);
    assert(missing.flush() && Storage.writes == 0);
    Storage.files[path] = "{broken";
    Store broken;
    assert(broken.get().crossPath == "/" && broken.get().uglySort == SortMode::Name);
    std::puts("state: missing and broken use defaults without writes");
  }
  {
    Storage = {};
    Store pending;
    for (int i = 1; i <= 5; ++i) pending.note(false, "/folder" + std::to_string(i), SortMode::Modified);
    assert(Storage.writes == 0);
    assert(pending.flush());
    assert(Storage.writes == 1);
    Store wake;
    assert(wake.get().crossPath == "/folder5");
    assert(wake.get().crossSort == SortMode::Modified && wake.get().uglySort == SortMode::Name);
    pending.note(false, "/folder5", SortMode::Modified);
    assert(pending.flush() && Storage.writes == 1);
    pending.note(true, "/Truyện", SortMode::Size);
    assert(pending.flush() && Storage.writes == 2);
    Store otherWake;
    assert(otherWake.get().crossPath == "/folder5" && otherWake.get().uglyPath == "/Truyện");
    assert(otherWake.get().uglySort == SortMode::Size);
    std::puts("state: 5 folders coalesce to 1 write, shell values survive a new store");
  }
  {
    Storage = {};
    Storage.files[path] = R"({"version":1,"crossPath":"/../book","uglyPath":5,"crossSort":-1,"uglySort":"2"})";
    Store invalid;
    assert(invalid.get().crossPath == "/" && invalid.get().uglyPath == "/");
    assert(invalid.get().crossSort == SortMode::Name && invalid.get().uglySort == SortMode::Name);
    invalid.note(false, std::string(300, 'x'), SortMode::Size);
    invalid.note(false, "/a//b", SortMode::Size);
    invalid.note(false, "/a/", SortMode::Size);
    assert(invalid.flush() && Storage.writes == 0);
    Storage.files[path] = std::string(2049, 'x');
    const auto reads = Storage.readCount;
    Store oversized;
    assert(oversized.get().crossSort == SortMode::Name);
    assert(Storage.readCount == reads);
    std::puts("state: invalid fields and oversized input stay bounded");
  }
  for (int fault = 0; fault < 4; ++fault) {
    Storage = {};
    Store old;
    old.note(false, "/old", SortMode::Name);
    assert(old.flush());
    const auto previous = Storage.files[path];
    old.note(false, "/new", SortMode::Size);
    if (fault == 0) Storage.failWrite = true;
    if (fault == 1) Storage.failSync = true;
    if (fault == 2) Storage.failClose = true;
    if (fault == 3) Storage.failRename.insert(std::string(path) + ".davtmp");
    assert(!old.flush());
    assert(Storage.files[path] == previous);
    Storage.failWrite = Storage.failSync = Storage.failClose = false;
    Storage.failRename.clear();
    Store afterFault;
    assert(afterFault.get().crossPath == "/old");
    assert(old.flush());
    Store retried;
    assert(retried.get().crossPath == "/new");
  }
  std::puts("state: short write, sync, close and rename faults keep the old file and can retry");
  {
    Storage = {};
    Storage.files[std::string(path) + ".davbak"] = R"({"version":1,"crossPath":"/kept","crossSort":1})";
    Storage.files[std::string(path) + ".davtmp"] = "{partial";
    Store recovered;
    assert(recovered.get().crossPath == "/kept");
    assert(recovered.get().crossSort == SortMode::Modified);
    std::puts("state: interrupted replacement recovers the committed backup");
  }
}
