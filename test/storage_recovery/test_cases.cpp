int failed=0,total=0;
void check(bool ok,const char*n){++total;printf("%s %s\n",ok?"PASS":"FAIL",n);if(!ok)++failed;}
void reset(){Storage=SDCardManager{};Storage.files["book"]="old-book";Storage.files["settings"]="{\"saved\":1}";transferBody="new-book";transferResult=HttpDownloader::OK;invalidations=0;failInvalidation=false;}
int main(){
 reset();Storage.failRead=true;check(Storage.readFile("settings").isEmpty(),"signed SD read error rejected");
 reset();Storage.shortRead=2;check(Storage.readFile("settings").length()==11,"short SD reads preserve full JSON");
 reset();Storage.failClose=true;check(Storage.readFile("settings").isEmpty(),"SD close failure rejected before JSON parse");
 reset();Storage.files["settings.davbak"]="{\"saved\":0}";Storage.failRead=true;JsonDocument unavailable;
 check(!PersistableStoreBase::readDocFromFile("settings",unavailable)&&Storage.files["settings"]=="{\"saved\":1}"&&Storage.files.count("settings.davbak"),"SD read failure never rolls main back to backup");
 for(int fault=0;fault<6;++fault){reset();
  if(fault==0)Storage.failOpen=true;if(fault==1)Storage.failWrite=true;if(fault==2)Storage.failSync=true;if(fault==3)Storage.failClose=true;if(fault==4)Storage.failRename.insert("settings");if(fault==5)Storage.failRename.insert("settings.davtmp");
  bool saved=Storage.writeFile("settings",String("{\"saved\":2}"));
  check(!saved && Storage.files["settings"]=="{\"saved\":1}",(std::string("persist fault ")+std::to_string(fault)+" keeps old").c_str());
  Storage.failOpen=Storage.failWrite=Storage.failSync=Storage.failClose=false;Storage.failRename.clear();
  check(Storage.writeFile("settings",String("{\"saved\":2}"))&&Storage.files["settings"]=="{\"saved\":2}",(std::string("persist same-state retry ")+std::to_string(fault)).c_str());
 }
 reset();Storage.files["settings.davbak"]="{\"saved\":1}";Storage.files.erase("settings");JsonDocument recovered;
 check(PersistableStoreBase::readDocFromFile("settings",recovered)&&recovered["saved"]==1,"backup-only reboot recovery");
 reset();Storage.files["settings.davbak"]="{\"saved\":1}";Storage.files["settings"]="{broken";recovered.clear();
 check(PersistableStoreBase::readDocFromFile("settings",recovered)&&recovered["saved"]==1,"invalid main recovers valid backup");
 reset();Storage.files["settings"]="";Storage.files["settings.davbak"]="{\"saved\":1}";recovered.clear();
 check(PersistableStoreBase::readDocFromFile("settings",recovered)&&recovered["saved"]==1,"empty main recovers valid backup");
 reset();Storage.files["settings.davtmp"]="{\"saved\":2}";recovered.clear();
 check(PersistableStoreBase::readDocFromFile("settings",recovered)&&recovered["saved"]==1,"uncommitted staging never promoted");
 reset();JsonDocument doc;doc["saved"]=2;String::failConcat=true;bool ok=PersistableStoreBase::writeDocToFile("settings",doc);String::failConcat=false;
 check(!ok&&Storage.files["settings"]=="{\"saved\":1}","String allocation failure preserves old");
 struct Deny:ArduinoJson::Allocator{void*allocate(size_t)override{return nullptr;}void deallocate(void*)override{}void*reallocate(void*,size_t)override{return nullptr;}}deny;
 reset();JsonDocument noHeap(&deny);noHeap["saved"]=2;check(noHeap.overflowed(),"OOM injected into real ArduinoJson");
 ok=PersistableStoreBase::writeDocToFile("settings",noHeap);check(!ok&&Storage.files["settings"]=="{\"saved\":1}","overflow document rejected before SD");
 reset();Storage.failRemove=true;ok=PersistableStoreBase::writeDocToFile("settings",doc);check(ok&&Storage.files["settings"]=="{\"saved\":2}"&&Storage.files["settings.davbak"]=="{\"saved\":1}","cleanup failure is committed with backup retained");
 Storage.failRemove=false;doc["saved"]=3;ok=PersistableStoreBase::writeDocToFile("settings",doc);check(ok&&Storage.files["settings"]=="{\"saved\":3}","next JSON save validates main before retiring retained backup");
 reset();Storage.files["settings"]="{broken";Storage.files["settings.davbak"]="{\"saved\":1}";Storage.failRename.insert("settings.davbak");
 ok=PersistableStoreBase::writeDocToFile("settings",doc);check(!ok&&Storage.files["settings.davbak"]=="{\"saved\":1}","failed restore never deletes sole valid backup on save");
 for(int fault=0;fault<8;++fault){reset();
  if(fault==0)Storage.failOpen=true;if(fault==1)Storage.failWrite=true;if(fault==2)Storage.failSync=true;if(fault==3)Storage.failClose=true;if(fault==4)transferResult=HttpDownloader::HTTP_ERROR;if(fault==5)transferResult=HttpDownloader::ABORTED;if(fault==6)Storage.failRename.insert("book.davtmp");if(fault==7)transferBody="";
  auto result=HttpDownloader::downloadToFile("https://fixture/book","book");
  check(result!=HttpDownloader::OK&&Storage.files["book"]=="old-book"&&invalidations==0,(std::string("download fault ")+std::to_string(fault)+" keeps old/cache").c_str());
  Storage.failOpen=Storage.failWrite=Storage.failSync=Storage.failClose=false;Storage.failRename.clear();transferBody="new-book";transferResult=HttpDownloader::OK;
  auto retried=HttpDownloader::downloadToFile("https://fixture/book","book");
  check(retried==HttpDownloader::OK&&Storage.files["book"]=="new-book"&&invalidations==1,(std::string("download same-state retry ")+std::to_string(fault)).c_str());
 }
 reset();failInvalidation=true;auto cacheError=HttpDownloader::downloadToFile("https://fixture/book","book");
 check(cacheError!=HttpDownloader::OK&&Storage.files["book"]=="new-book"&&invalidations==1,"postcommit cache failure reports error while retaining committed content");
 reset();Storage.failRemove=true;auto pending=HttpDownloader::downloadToFile("https://fixture/book","book");
 Storage.failRemove=false;transferBody="third-book";auto retry=HttpDownloader::downloadToFile("https://fixture/book","book");
 check(pending==HttpDownloader::OK&&retry==HttpDownloader::FILE_ERROR&&Storage.files["book"]=="new-book"&&Storage.files["book.davbak"]=="old-book"&&invalidations==1,"pending cleanup blocks retry while preserving both good versions");
 reset();auto result=HttpDownloader::downloadToFile("https://fixture/book","book");check(result==HttpDownloader::OK&&Storage.files["book"]=="new-book"&&invalidations==1,"download commits before cache invalidation");
 transferBody="third-book";result=HttpDownloader::downloadToFile("https://fixture/book","book");check(result==HttpDownloader::OK&&Storage.files["book"]=="third-book"&&invalidations==2,"two ordinary downloads replace consecutively");
 reset();Storage.files["book.epub"]="old-epub";transferBody="broken-epub";result=HttpDownloader::downloadToFile("https://fixture/book","book.epub");check(result!=HttpDownloader::OK&&Storage.files["book.epub"]=="old-epub","invalid EPUB structure preserves old book");
 printf("RESULT %d/%d passed\n",total-failed,total);return failed?1:0;
}
