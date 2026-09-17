/*
 * tenor/cross shared web locale helper (WebI18n.inc).
 *
 * This is the only web translation/selection implementation. It is injected by
 * scripts/build_html.py at each page's TENOR_WEB_I18N marker inside that page's
 * script block, so a page never carries its own catalogue.
 *
 * Locales: 'vi' (default), 'en-AU', 'zh-Hans'.
 * Precedence: valid saved localStorage['tenor-web-language'] wins, else a valid
 * ?lang= query parameter, else 'vi'. Persisting is best-effort: when storage is
 * unavailable the choice stays active for the page and keeps travelling in the
 * ?lang= parameter on same-origin links and requests. Web language never
 * changes the device language.
 *
 * Public API (stable contract for Home/Files/Settings/Fonts pages):
 *   TenorI18n.init()                  resolve locale, mount selectors, translate.
 *   TenorI18n.t(key, params)          keyed message, {name} placeholders.
 *   TenorI18n.tPlural(base, n, p)     base + '.one'/'.other' (vi/zh use '.other').
 *   TenorI18n.locale()                current locale code.
 *   TenorI18n.setLocale(code)         switch, persist, re-render, notify.
 *   TenorI18n.onChange(fn)            subscribe to locale changes (returns off()).
 *   TenorI18n.withLang(url)           same-origin URL with ?lang=; external URLs
 *                                     and mailto:/tel:/# targets are untouched.
 *   TenorI18n.apiUrl(url)             alias of withLang for fetch() targets.
 *   TenorI18n.bind(sel, key, attr?)   translate markup this page cannot annotate
 *                                     (e.g. the shared BrandHeader tagline).
 *   TenorI18n.apply(root?)            re-apply translations, lang attribute,
 *                                     selector state, bindings and link locale.
 *   TenorI18n.mountSelector(el)       build the accessible picker in `el`.
 *   TenorI18n.formatNumber(n)         locale-aware number grouping.
 *   TenorI18n.missing                 keys observed missing while translating.
 *
 * Markup hooks:
 *   [data-i18n="key"]                 element text becomes the message.
 *   [data-i18n-title|aria-label|placeholder="key"]  that attribute is set.
 *   [data-tenor-lang-selector]        host for the shared language picker.
 *
 * Messages are written with textContent/attributes only, never innerHTML, so a
 * translated string can never inject markup. Key namespaces: nav./lang./brand./
 * common. are shared by every page; home./fonts. belong to Home/Fonts, and the
 * later Files/Settings work appends files./settings. keys to this same table.
 */
var TenorI18n = (function () {
  'use strict';

  var LOCALES = ['vi', 'en-AU', 'zh-Hans'];
  var DEFAULT_LOCALE = 'vi';
  var STORAGE_KEY = 'tenor-web-language';
  var LANG_PARAM = 'lang';
  var SELECTOR_ATTR = 'data-tenor-lang-selector';
  var SELECT_CLASS = 'tenor-lang-select';
  var LABEL_CLASS = 'tenor-lang-label';

  // Language names stay in their own language; the selector is shared by every
  // page and is never translated.
  var OPTIONS = [
    { code: 'vi', label: 'Tiếng Việt' },
    { code: 'en-AU', label: 'English (AU)' },
    { code: 'zh-Hans', label: '简体中文' }
  ];

  var CATALOGUES = {
    vi: {
      'nav.device': 'Thiết bị',
      'nav.files': 'Tệp',
      'nav.settings': 'Cài đặt',
      'nav.fonts': 'Font chữ',
      'lang.label': 'Ngôn ngữ',
      'brand.localTagline': 'Thư viện qua Wi-Fi',
      'common.na': 'N/A',
      'common.loading': 'Đang tải...',
      'common.bytes': '{value} bytes',
      'home.device': 'Thiết bị',
      'home.serial': 'Số máy',
      'home.version': 'Phiên bản',
      'home.wifi': 'Kết nối Wi-Fi',
      'home.ip': 'Địa chỉ IP',
      'home.freeHeap': 'Bộ nhớ trống',
      'home.wifiConnected': 'Đã kết nối',
      'home.footerLibrary': 'Thư viện trên thiết bị',
      'fonts.docTitle': 'tenor/cross - Font chữ',
      'fonts.installedTitle': 'Font đã cài',
      'fonts.addTitle': 'Thêm font chữ',
      'fonts.submit': 'Gửi font',
      'fonts.empty': 'Chưa cài font nào',
      'fonts.delete': 'Xóa',
      'fonts.deleteConfirm': 'Xóa bộ font "{family}"?',
      'fonts.deleting': 'Đang xóa {family}...',
      'fonts.deleted': 'Đã xóa "{family}".',
      'fonts.deleteFailed': 'Không xóa được "{family}".',
      'fonts.deleteError': 'Lỗi khi xóa: {message}',
      'fonts.loadFailed': 'Không tải được danh sách font',
      'fonts.familyMeta': '{sizes}pt · {files}',
      'fonts.noCpfontInFolder': 'Không tìm thấy tệp .cpfont nào trong thư mục đã chọn.',
      'fonts.pickedSummary.other': '{count} tệp → bộ font "{family}"',
      'fonts.noSelection': 'Chưa chọn tệp .cpfont nào.',
      'fonts.multiFamily': 'Vui lòng chọn tệp trong cùng một bộ font.',
      'fonts.uploading': 'Đang gửi {index}/{total}: {file}',
      'fonts.uploadFailedFile': 'Lỗi ở {file}: {message}',
      'fonts.uploadError': 'Lỗi khi gửi {file}: {message}',
      'fonts.uploadedSummary.other': 'Đã gửi {count} tệp vào bộ font "{family}".',
      'fonts.unknownError': 'lỗi không rõ',
      'files.docTitle': 'Tệp - tenor/cross',
      'files.yourFiles': 'Tệp của bạn',
      'files.memoryCard': 'Thẻ nhớ',
      'files.upload': 'Gửi tệp',
      'files.optimizeAndUpload': 'Tối ưu và gửi',
      'files.newFolder': 'Thư mục mới',
      'files.deleteSelected': 'Xóa mục đã chọn',
      'files.failedUploadsTitle': 'Một số tệp gửi chưa thành công',
      'files.dismiss': 'Bỏ qua',
      'files.retryFailed': 'Gửi lại các tệp lỗi',
      'files.retry': 'Thử lại',
      'files.failedFileError': 'Lỗi: {message}',
      'files.convertWarning': 'Chuyển đổi sẽ thay đổi tệp và có thể phá vỡ đồng bộ dựa trên mã băm.',
      'files.convertWarningBackup': 'Hãy sao lưu hoặc tắt đồng bộ trước khi tiếp tục.',
      'files.chooseFilesInto': 'Chọn tệp để gửi vào',
      'files.chooseFilesLabel': 'Chọn tệp để gửi',
      'files.dropHint': 'Kéo tệp vào đây hoặc bấm để chọn',
      'files.optimizeEpub': 'Tối ưu EPUB',
      'files.advancedOptions': 'Tùy chọn nâng cao',
      'files.renameFromMetadata': 'Đổi tên theo siêu dữ liệu sách',
      'files.renameFromMetadataDesc': 'Dùng Title - Author.epub khi có',
      'files.jpegQuality': '📦 Chất lượng JPEG',
      'files.qualityMin': 'Chất lượng thấp nhất',
      'files.qualityLow': 'Chất lượng thấp',
      'files.qualityMediumLow': 'Chất lượng trung bình thấp',
      'files.qualityMedium': 'Chất lượng trung bình',
      'files.qualityHigh': 'Chất lượng cao',
      'files.qualityMax': 'Chất lượng cao nhất',
      'files.jpegSummary': '📦 {quality}% JPEG',
      'files.maxSize': '📏 Tối đa {width}×{height}px',
      'files.trueGrayscale': '⚫ Thang xám thật',
      'files.fixSvg': '🔧 Sửa SVG',
      'files.autoCrop': '✂ Tự cắt viền',
      'files.autoCropDesc': 'Cắt viền đồng nhất và co nội dung ảnh cho vừa',
      'files.targetDevice': '📱 Thiết bị đích',
      'files.auto': 'Tự động',
      'files.autoWith': 'Tự động ({device})',
      'files.rotationDirection': '↻ Chiều xoay',
      'files.rotationCcw': 'CCW',
      'files.rotationCw': 'CW',
      'files.minOverlap': '📐 Chồng lấn tối thiểu',
      'files.autoDownloadLog': '📥 Tự động tải nhật ký',
      'files.autoDownloadLogDesc': 'Xuất nhật ký chi tiết kèm thống kê',
      'files.rememberSettings': 'Ghi nhớ cài đặt',
      'files.rememberSettingsDesc': 'Lưu các tùy chọn gửi này trong trình duyệt',
      'files.imageOptions': '🖼️ Tùy chọn xử lý ảnh',
      'files.imageCountAllSelectable': '{count} ảnh (tất cả đều chọn được)',
      'files.imageCountSelectable': '{count} ảnh ({selectable} chọn được, {locked})',
      'files.imageCoverLocked': '{width}×{height} - Ảnh bìa (khóa)',
      'files.imageSeparatorLocked': '{width}×{height} - Ảnh phân cách (khóa)',
      'files.partsCount': ' ({count} phần)',
      'files.autoCropSize': ' | Tự cắt: {width}×{height}',
      'files.stateNormal': 'Bình thường',
      'files.stateHSplit': 'H-Split',
      'files.stateVSplit': 'V-Split',
      'files.stateRotate': 'Xoay',
      'files.legendCover': 'Ảnh bìa',
      'files.legendSeparator': 'Ảnh phân cách',
      'files.previewFailed': 'Không xem trước được ảnh: {message}\n\nQuá trình chuyển đổi vẫn tiếp tục.',
      'files.jsZipMissing': 'Thiếu thư viện JSZip. Quá trình chuyển đổi vẫn tiếp tục mà không có bộ chọn ảnh.',
      'files.cancel': 'Hủy',
      'files.selectAll': 'Chọn tất cả',
      'files.selectItem': 'Chọn {name}',
      'files.type': 'Loại',
      'files.sizeColumn': 'Dung lượng',
      'files.actions': 'Thao tác',
      'files.folderBadge': 'Thư mục',
      'files.deleteFolderTitle': 'Xóa thư mục',
      'files.deleteFileTitle': 'Xóa tệp',
      'files.moveFileTitle': 'Di chuyển tệp',
      'files.renameFileTitle': 'Đổi tên tệp',
      'files.emptyFolder': 'Thư mục trống',
      'files.loadError': 'Đã xảy ra lỗi khi tải danh sách tệp',
      'files.folderSummary': '{folders}, {files}, {size}',
      'files.download': 'Tải xuống',
      'files.imagePreview': 'Xem ảnh',
      'files.networkRestored': 'Đã khôi phục kết nối mạng',
      'files.networkLost': 'Mất kết nối mạng',
      'files.uploadCancelled': 'Đã hủy gửi tệp.',
      'files.allUploadsComplete': 'Đã gửi xong tất cả tệp.',
      'files.uploadedPartial': '{done}/{total} đã gửi. Lỗi: {failed}',
      'files.readingMetadata': 'Đang đọc siêu dữ liệu cho {file} ({index}/{total})...',
      'files.stageUploading': 'Đang gửi',
      'files.stageConverting': 'Đang chuyển đổi và gửi',
      'files.uploadingFile': '{stage} {file} ({index}/{total}){method}',
      'files.uploadingFileProgress': '{stage} {file} ({index}/{total}){method} - {percent}%',
      'files.convertingFile': 'Đang chuyển đổi {file} ({index}/{total})...',
      'files.conversionFailedUploadOriginal': 'Chuyển đổi lỗi, đang gửi tệp gốc {file}...',
      'files.uploadComplete': 'Đã gửi xong: {file}',
      'files.networkErrorInterrupted': 'Lỗi mạng - gửi bị gián đoạn',
      'files.selectAtLeastOneFile': 'Vui lòng chọn ít nhất một tệp.',
      'files.checkExistingFailed': 'Không kiểm tra được tệp hiện có: {message}',
      'files.selectItemToDelete': 'Vui lòng chọn ít nhất một mục để xóa.',
      'files.deleteWarning': '⚠️ Không thể hoàn tác thao tác này!',
      'files.deleteConfirmText': 'Bạn có chắc muốn xóa các mục sau?',
      'files.delete': 'Xóa',
      'files.deleteFailed': 'Xóa thất bại: {message}',
      'files.deleteNetworkError': 'Xóa thất bại - lỗi mạng',
      'files.folderNameRequired': 'Vui lòng nhập tên thư mục.',
      'files.folderNameInvalid': 'Tên thư mục không được chứa " * : < > ? / \\ | và không được là . hoặc ..',
      'files.createFolderIn': 'Tạo thư mục mới trong',
      'files.folderNamePlaceholder': 'Tên thư mục...',
      'files.createFolder': 'Tạo thư mục',
      'files.createFolderFailed': 'Tạo thư mục thất bại: {message}',
      'files.createFolderNetworkError': 'Tạo thư mục thất bại - lỗi mạng',
      'files.renameTitle': 'Đổi tên tệp',
      'files.renaming': 'Đang đổi tên',
      'files.newNamePlaceholder': 'Tên tệp mới...',
      'files.rename': 'Đổi tên',
      'files.newNameRequired': 'Vui lòng nhập tên mới.',
      'files.nameNoSlashes': 'Tên tệp không được chứa dấu /.',
      'files.renameFailed': 'Đổi tên thất bại: {message}',
      'files.renameNetworkError': 'Đổi tên thất bại - lỗi mạng',
      'files.moveTitle': 'Di chuyển tệp',
      'files.moving': 'Đang di chuyển',
      'files.moveDestPlaceholder': '/Thư mục/Đích',
      'files.move': 'Di chuyển',
      'files.moveDestRequired': 'Vui lòng nhập thư mục đích.',
      'files.moveFailed': 'Di chuyển thất bại: {message}',
      'files.moveNetworkError': 'Di chuyển thất bại - lỗi mạng',
      'files.conversionLog': '📋 Nhật ký chuyển đổi',
      'files.logStartingBatch': 'Bắt đầu chuyển đổi hàng loạt: {count} tệp',
      'files.logSettingsLine': 'Chất lượng: {quality}% | Chồng lấn: {overlap}% | Xoay: {rotation} | Thang xám: {grayscale} | Tự cắt: {crop}',
      'files.on': 'Bật',
      'files.off': 'Tắt',
      'files.logConversionComplete': 'Chuyển đổi hoàn tất!',
      'files.logUploadOriginal': 'Đang gửi tệp gốc...',
      'files.logUploadFailed': 'Gửi tệp lỗi: {message}',
      'files.logConversionFailed': 'Chuyển đổi lỗi: {message}',
      'files.logGenerated': 'Được tạo: {date}',
      'files.logImageFailed': 'Cảnh báo: không xử lý được {file}, dùng ảnh gốc',
      'files.logRemovedFonts': 'Đã xóa {fonts} font nhúng và {rules} mục @font-face, {size} dữ liệu font',
      'files.logRemovedFontFile': 'Đã xóa font nhúng {file} ({size})',
      'files.logPartsCount': '{count} phần',
      'files.logSummaryTitle': '📊 Tóm tắt chuyển đổi',
      'files.logBatchSummaryTitle': '📊 Tóm tắt chuyển đổi hàng loạt',
      'files.logImagesFound': 'Số ảnh tìm thấy',
      'files.logImagesProcessed': 'Số ảnh đã xử lý',
      'files.logRepairs': 'Sửa EPUB',
      'files.logFixesApplied': '{count} sửa đổi',
      'files.logNoneNeeded': 'Không cần',
      'files.logErrors': 'Lỗi',
      'files.logOriginalSize': 'Dung lượng gốc',
      'files.logOptimizedSize': 'Dung lượng tối ưu',
      'files.logSaved': 'Tiết kiệm',
      'files.logTime': 'Thời gian',
      'files.logFilesProcessed': 'Số tệp đã xử lý',
      'files.logSuccessful': 'Thành công',
      'files.logFailed': 'Thất bại',
      'files.logTotalImages': 'Tổng số ảnh đã xử lý',
      'files.logTotalSplits': 'Tổng số lần tách',
      'files.logTotalFixes': 'Tổng số sửa đổi',
      'files.logTotalErrors': 'Tổng số lỗi',
      'files.logTotalOriginal': 'Tổng dung lượng gốc',
      'files.logTotalOptimized': 'Tổng dung lượng tối ưu',
      'files.logTotalSaved': 'Tổng tiết kiệm',
      'files.logTotalTime': 'Tổng thời gian',
      'files.logSplitInto': 'thành {count} phần',
      'files.logFixComments': 'Chú thích ({count}, {size})',
      'files.logFixSvgCover': 'Ảnh bìa SVG',
      'files.logFixSvgImages': 'Ảnh SVG ({count})',
      'files.logFixFontFaceRules': '{count} mục đã xóa khỏi {file}',
      'files.logFixOpfDetail': 'đã cập nhật manifest',
      'files.logFixNcxDetail': 'Đã đồng bộ với OPF',
      'files.logFixEncryptionDropped': 'đã xóa (chỉ chứa mục che giấu font)',
      'files.logFixEncryptionObfuscated': 'đã xóa các mục che giấu font',
      'settings.docTitle': 'Cài đặt - tenor/cross',
      'settings.save': 'Lưu cài đặt',
      'settings.saving': 'Đang lưu...',
      'settings.noChanges': 'Không có thay đổi để lưu.',
      'settings.saved': 'Đã lưu cài đặt.',
      'settings.error': 'Lỗi: {message}',
      'settings.loadFailed': 'Không tải được cài đặt',
      'settings.password': 'Mật khẩu',
      'settings.username': 'Tên người dùng',
      'settings.serverName': 'Tên máy chủ',
      'settings.unchanged': '(không đổi)',
      'settings.lastConnected': 'Mạng kết nối gần nhất',
      'settings.wifiTitle': 'Mạng Wi-Fi',
      'settings.wifiEmpty': 'Chưa lưu mạng Wi-Fi nào',
      'settings.addNetwork': 'Thêm mạng',
      'settings.remove': 'Xóa',
      'settings.saveOne': 'Lưu',
      'settings.ssidRequired': 'Cần nhập SSID.',
      'settings.wifiSaved': 'Đã lưu mạng Wi-Fi.',
      'settings.wifiDeleted': 'Đã xóa mạng Wi-Fi.',
      'settings.wifiDeleteConfirm': 'Xóa mạng Wi-Fi này?',
      'settings.opdsTitle': 'Máy chủ OPDS',
      'settings.opdsEmpty': 'Chưa cấu hình máy chủ OPDS',
      'settings.addServer': 'Thêm máy chủ',
      'settings.opdsSaved': 'Đã lưu máy chủ OPDS.',
      'settings.opdsDeleted': 'Đã xóa máy chủ OPDS.',
      'settings.opdsDeleteConfirm': 'Xóa máy chủ OPDS này?',
      'files.coverCount.other': '{count} ảnh bìa',
      'files.separatorCount.other': '{count} ảnh phân cách',
      'files.folderCount.other': '{count} thư mục',
      'files.fileCount.other': '{count} tệp',
      'files.imageCount.other': '{count} ảnh'
    },
    'en-AU': {
      'nav.device': 'Device',
      'nav.files': 'Files',
      'nav.settings': 'Settings',
      'nav.fonts': 'Fonts',
      'lang.label': 'Language',
      'brand.localTagline': 'Wi-Fi library',
      'common.na': 'N/A',
      'common.loading': 'Loading...',
      'common.bytes': '{value} bytes',
      'home.device': 'Device',
      'home.serial': 'Serial',
      'home.version': 'Version',
      'home.wifi': 'Wi-Fi connection',
      'home.ip': 'IP address',
      'home.freeHeap': 'Free memory',
      'home.wifiConnected': 'Connected',
      'home.footerLibrary': 'Library on device',
      'fonts.docTitle': 'tenor/cross - Fonts',
      'fonts.installedTitle': 'Installed fonts',
      'fonts.addTitle': 'Add fonts',
      'fonts.submit': 'Upload fonts',
      'fonts.empty': 'No fonts installed',
      'fonts.delete': 'Delete',
      'fonts.deleteConfirm': 'Delete font family "{family}"?',
      'fonts.deleting': 'Deleting {family}...',
      'fonts.deleted': 'Deleted "{family}".',
      'fonts.deleteFailed': 'Failed to delete "{family}".',
      'fonts.deleteError': 'Delete error: {message}',
      'fonts.loadFailed': 'Failed to load font list',
      'fonts.familyMeta': '{sizes}pt · {files}',
      'fonts.noCpfontInFolder': 'No .cpfont files found in the selected folder.',
      'fonts.pickedSummary.one': '{count} file → family "{family}"',
      'fonts.pickedSummary.other': '{count} files → family "{family}"',
      'fonts.noSelection': 'No .cpfont files selected.',
      'fonts.multiFamily': 'Please select files from a single font family.',
      'fonts.uploading': 'Uploading {index}/{total}: {file}',
      'fonts.uploadFailedFile': 'Failed on {file}: {message}',
      'fonts.uploadError': 'Upload error on {file}: {message}',
      'fonts.uploadedSummary.one': 'Uploaded {count} file to family "{family}".',
      'fonts.uploadedSummary.other': 'Uploaded {count} files to family "{family}".',
      'fonts.unknownError': 'unknown error',
      'files.docTitle': 'Files - tenor/cross',
      'files.yourFiles': 'Your files',
      'files.memoryCard': 'Memory card',
      'files.upload': 'Upload',
      'files.optimizeAndUpload': 'Optimise & Upload',
      'files.newFolder': 'New folder',
      'files.deleteSelected': 'Delete selected',
      'files.failedUploadsTitle': 'Some uploads failed',
      'files.dismiss': 'Dismiss',
      'files.retryFailed': 'Retry failed files',
      'files.retry': 'Retry',
      'files.failedFileError': 'Error: {message}',
      'files.convertWarning': 'Converting modifies files and can break hash-based sync.',
      'files.convertWarningBackup': 'Back up or disable sync before proceeding.',
      'files.chooseFilesInto': 'Choose files to upload into',
      'files.chooseFilesLabel': 'Choose files to upload',
      'files.dropHint': 'Drop files here or click to choose',
      'files.optimizeEpub': 'Optimise EPUB',
      'files.advancedOptions': 'Advanced options',
      'files.renameFromMetadata': 'Rename from Book Metadata',
      'files.renameFromMetadataDesc': 'Use Title - Author.epub when available',
      'files.jpegQuality': '📦 JPEG Quality',
      'files.qualityMin': 'Minimum quality',
      'files.qualityLow': 'Low quality',
      'files.qualityMediumLow': 'Medium-low quality',
      'files.qualityMedium': 'Medium quality',
      'files.qualityHigh': 'High quality',
      'files.qualityMax': 'Maximum quality',
      'files.jpegSummary': '📦 {quality}% JPEG',
      'files.maxSize': '📏 Max {width}×{height}px',
      'files.trueGrayscale': '⚫ True-Grayscale',
      'files.fixSvg': '🔧 Fix SVG',
      'files.autoCrop': '✂ Auto-crop Margins',
      'files.autoCropDesc': 'Trim uniform borders and scale image content to fit',
      'files.targetDevice': '📱 Target Device',
      'files.auto': 'Auto',
      'files.autoWith': 'Auto ({device})',
      'files.rotationDirection': '↻ Rotation Direction',
      'files.rotationCcw': 'CCW',
      'files.rotationCw': 'CW',
      'files.minOverlap': '📐 Min Overlap',
      'files.autoDownloadLog': '📥 Auto-download Log',
      'files.autoDownloadLogDesc': 'Export detailed log with statistics',
      'files.rememberSettings': 'Remember Settings',
      'files.rememberSettingsDesc': 'Store these upload options in this browser',
      'files.imageOptions': '🖼️ Image Processing Options',
      'files.imageCountAllSelectable': '{count} images (all selectable)',
      'files.imageCountSelectable': '{count} images ({selectable} selectable, {locked})',
      'files.imageCoverLocked': '{width}×{height} - Cover image (locked)',
      'files.imageSeparatorLocked': '{width}×{height} - Separator (locked)',
      'files.partsCount': ' ({count} parts)',
      'files.autoCropSize': ' | Auto-crop: {width}×{height}',
      'files.stateNormal': 'Normal',
      'files.stateHSplit': 'H-Split',
      'files.stateVSplit': 'V-Split',
      'files.stateRotate': 'Rotate',
      'files.legendCover': 'Cover',
      'files.legendSeparator': 'Separator',
      'files.previewFailed': 'Failed to preview images: {message}\n\nConversion will proceed normally.',
      'files.jsZipMissing': 'JSZip library not available. Conversion will proceed without image picker.',
      'files.cancel': 'Cancel',
      'files.selectAll': 'Select all',
      'files.selectItem': 'Select {name}',
      'files.type': 'Type',
      'files.sizeColumn': 'Size',
      'files.actions': 'Actions',
      'files.folderBadge': 'Folder',
      'files.deleteFolderTitle': 'Delete folder',
      'files.deleteFileTitle': 'Delete file',
      'files.moveFileTitle': 'Move file',
      'files.renameFileTitle': 'Rename file',
      'files.emptyFolder': 'Folder is empty',
      'files.loadError': 'An error occurred while loading the files',
      'files.folderSummary': '{folders}, {files}, {size}',
      'files.download': 'Download',
      'files.imagePreview': 'Image preview',
      'files.networkRestored': 'Network connection restored',
      'files.networkLost': 'Network connection lost',
      'files.uploadCancelled': 'Upload cancelled.',
      'files.allUploadsComplete': 'All uploads complete.',
      'files.uploadedPartial': '{done}/{total} uploaded. Failed: {failed}',
      'files.readingMetadata': 'Reading metadata for {file} ({index}/{total})...',
      'files.stageUploading': 'Uploading',
      'files.stageConverting': 'Converting & uploading',
      'files.uploadingFile': '{stage} {file} ({index}/{total}){method}',
      'files.uploadingFileProgress': '{stage} {file} ({index}/{total}){method} - {percent}%',
      'files.convertingFile': 'Converting {file} ({index}/{total})...',
      'files.conversionFailedUploadOriginal': 'Conversion failed, uploading original {file}...',
      'files.uploadComplete': 'Upload complete: {file}',
      'files.networkErrorInterrupted': 'Network error - upload interrupted',
      'files.selectAtLeastOneFile': 'Please select at least one file.',
      'files.checkExistingFailed': 'Failed to check existing files: {message}',
      'files.selectItemToDelete': 'Please select at least one item to delete.',
      'files.deleteWarning': '⚠️ This action cannot be undone!',
      'files.deleteConfirmText': 'Are you sure you want to delete the following item(s)?',
      'files.delete': 'Delete',
      'files.deleteFailed': 'Delete failed: {message}',
      'files.deleteNetworkError': 'Delete failed - network error',
      'files.folderNameRequired': 'Please enter a folder name.',
      'files.folderNameInvalid': 'Folder name cannot contain " * : < > ? / \\ | and must not be . or ..',
      'files.createFolderIn': 'Create a new folder in',
      'files.folderNamePlaceholder': 'Folder name...',
      'files.createFolder': 'Create folder',
      'files.createFolderFailed': 'Failed to create folder: {message}',
      'files.createFolderNetworkError': 'Failed to create folder - network error',
      'files.renameTitle': 'Rename file',
      'files.renaming': 'Renaming',
      'files.newNamePlaceholder': 'New file name...',
      'files.rename': 'Rename',
      'files.newNameRequired': 'Please enter a new name.',
      'files.nameNoSlashes': 'File name cannot include slashes.',
      'files.renameFailed': 'Failed to rename: {message}',
      'files.renameNetworkError': 'Failed to rename - network error',
      'files.moveTitle': 'Move file',
      'files.moving': 'Moving',
      'files.moveDestPlaceholder': '/Destination/Folder',
      'files.move': 'Move',
      'files.moveDestRequired': 'Please enter a destination folder.',
      'files.moveFailed': 'Failed to move: {message}',
      'files.moveNetworkError': 'Failed to move - network error',
      'files.conversionLog': '📋 Conversion Log',
      'files.logStartingBatch': 'Starting batch conversion: {count} file(s)',
      'files.logSettingsLine': 'Quality: {quality}% | Overlap: {overlap}% | Rotation: {rotation} | Grayscale: {grayscale} | Auto-crop: {crop}',
      'files.on': 'On',
      'files.off': 'Off',
      'files.logConversionComplete': 'Conversion complete!',
      'files.logUploadOriginal': 'Uploading original file instead...',
      'files.logUploadFailed': 'Upload failed: {message}',
      'files.logConversionFailed': 'Conversion failed: {message}',
      'files.logGenerated': 'Generated: {date}',
      'files.logImageFailed': 'Warning: failed to process {file}, using original',
      'files.logRemovedFonts': 'Removed {fonts} embedded font(s) and {rules} @font-face rule(s), {size} of font data',
      'files.logRemovedFontFile': 'Removed embedded font {file} ({size})',
      'files.logPartsCount': '{count} parts',
      'files.logSummaryTitle': '📊 Conversion Summary',
      'files.logBatchSummaryTitle': '📊 Batch Conversion Summary',
      'files.logImagesFound': 'Images found',
      'files.logImagesProcessed': 'Images processed',
      'files.logRepairs': 'EPUB repairs',
      'files.logFixesApplied': '{count} fixes applied',
      'files.logNoneNeeded': 'None needed',
      'files.logErrors': 'Errors',
      'files.logOriginalSize': 'Original size',
      'files.logOptimizedSize': 'Optimised size',
      'files.logSaved': 'Saved',
      'files.logTime': 'Time',
      'files.logFilesProcessed': 'Files processed',
      'files.logSuccessful': 'Successful',
      'files.logFailed': 'Failed',
      'files.logTotalImages': 'Total images processed',
      'files.logTotalSplits': 'Total splits',
      'files.logTotalFixes': 'Total fixes applied',
      'files.logTotalErrors': 'Total errors',
      'files.logTotalOriginal': 'Total original',
      'files.logTotalOptimized': 'Total optimised',
      'files.logTotalSaved': 'Total saved',
      'files.logTotalTime': 'Total time',
      'files.logSplitInto': 'into {count} parts',
      'files.logFixComments': 'Comments ({count}, {size})',
      'files.logFixSvgCover': 'SVG cover',
      'files.logFixSvgImages': 'SVG images ({count})',
      'files.logFixFontFaceRules': '{count} rule(s) removed from {file}',
      'files.logFixOpfDetail': 'manifest updated',
      'files.logFixNcxDetail': 'Synced with OPF',
      'files.logFixEncryptionDropped': 'removed (contained only font obfuscation entries)',
      'files.logFixEncryptionObfuscated': 'font obfuscation entries removed',
      'settings.docTitle': 'Settings - tenor/cross',
      'settings.save': 'Save settings',
      'settings.saving': 'Saving...',
      'settings.noChanges': 'No changes to save.',
      'settings.saved': 'Settings saved.',
      'settings.error': 'Error: {message}',
      'settings.loadFailed': 'Failed to load settings',
      'settings.password': 'Password',
      'settings.username': 'Username',
      'settings.serverName': 'Server name',
      'settings.unchanged': '(unchanged)',
      'settings.lastConnected': 'Last connected network',
      'settings.wifiTitle': 'Wi-Fi networks',
      'settings.wifiEmpty': 'No Wi-Fi networks saved',
      'settings.addNetwork': 'Add network',
      'settings.remove': 'Delete',
      'settings.saveOne': 'Save',
      'settings.ssidRequired': 'SSID is required.',
      'settings.wifiSaved': 'Wi-Fi network saved.',
      'settings.wifiDeleted': 'Wi-Fi network deleted.',
      'settings.wifiDeleteConfirm': 'Delete this Wi-Fi network?',
      'settings.opdsTitle': 'OPDS servers',
      'settings.opdsEmpty': 'No OPDS servers configured',
      'settings.addServer': 'Add server',
      'settings.opdsSaved': 'OPDS server saved.',
      'settings.opdsDeleted': 'OPDS server deleted.',
      'settings.opdsDeleteConfirm': 'Delete this OPDS server?',
      'files.coverCount.one': '{count} cover',
      'files.coverCount.other': '{count} covers',
      'files.separatorCount.one': '{count} separator',
      'files.separatorCount.other': '{count} separators',
      'files.folderCount.one': '{count} folder',
      'files.folderCount.other': '{count} folders',
      'files.fileCount.one': '{count} file',
      'files.fileCount.other': '{count} files',
      'files.imageCount.one': '{count} image',
      'files.imageCount.other': '{count} images'
    },
    'zh-Hans': {
      'nav.device': '设备',
      'nav.files': '文件',
      'nav.settings': '设置',
      'nav.fonts': '字体',
      'lang.label': '语言',
      'brand.localTagline': 'Wi-Fi 书库',
      'common.na': '无',
      'common.loading': '正在加载...',
      'common.bytes': '{value} 字节',
      'home.device': '设备',
      'home.serial': '序列号',
      'home.version': '版本',
      'home.wifi': 'Wi-Fi 连接',
      'home.ip': 'IP 地址',
      'home.freeHeap': '可用内存',
      'home.wifiConnected': '已连接',
      'home.footerLibrary': '设备上的书库',
      'fonts.docTitle': 'tenor/cross - 字体',
      'fonts.installedTitle': '已安装字体',
      'fonts.addTitle': '添加字体',
      'fonts.submit': '上传字体',
      'fonts.empty': '尚未安装字体',
      'fonts.delete': '删除',
      'fonts.deleteConfirm': '删除字体家族“{family}”？',
      'fonts.deleting': '正在删除 {family}...',
      'fonts.deleted': '已删除“{family}”。',
      'fonts.deleteFailed': '无法删除“{family}”。',
      'fonts.deleteError': '删除出错：{message}',
      'fonts.loadFailed': '无法加载字体列表',
      'fonts.familyMeta': '{sizes}pt · {files}',
      'fonts.noCpfontInFolder': '所选文件夹中没有 .cpfont 文件。',
      'fonts.pickedSummary.other': '{count} 个文件 → 字体家族“{family}”',
      'fonts.noSelection': '未选择 .cpfont 文件。',
      'fonts.multiFamily': '请选择同一字体家族的文件。',
      'fonts.uploading': '正在上传 {index}/{total}：{file}',
      'fonts.uploadFailedFile': '{file} 处理失败：{message}',
      'fonts.uploadError': '上传 {file} 出错：{message}',
      'fonts.uploadedSummary.other': '已上传 {count} 个文件到字体家族“{family}”。',
      'fonts.unknownError': '未知错误',
      'files.docTitle': '文件 - tenor/cross',
      'files.yourFiles': '你的文件',
      'files.memoryCard': '存储卡',
      'files.upload': '上传文件',
      'files.optimizeAndUpload': '转换并上传',
      'files.newFolder': '新建文件夹',
      'files.deleteSelected': '删除所选项目',
      'files.failedUploadsTitle': '部分文件上传失败',
      'files.dismiss': '关闭',
      'files.retryFailed': '重新上传失败的文件',
      'files.retry': '重试',
      'files.failedFileError': '出错：{message}',
      'files.convertWarning': '转换修改文件，可能无法同步。',
      'files.convertWarningBackup': '继续前请先复制文件或关闭同步。',
      'files.chooseFilesInto': '选择文件上传到',
      'files.chooseFilesLabel': '选择要上传的文件',
      'files.dropHint': '将文件放在此处或点按选择',
      'files.optimizeEpub': '转换 EPUB',
      'files.advancedOptions': '更多选项',
      'files.renameFromMetadata': '使用书籍元数据重命名',
      'files.renameFromMetadataDesc': '可用时使用 Title - Author.epub',
      'files.jpegQuality': '📦 JPEG 压缩',
      'files.qualityMin': '最小',
      'files.qualityLow': '较小',
      'files.qualityMediumLow': '中下',
      'files.qualityMedium': '中',
      'files.qualityHigh': '较大',
      'files.qualityMax': '最大',
      'files.jpegSummary': '📦 {quality}% JPEG',
      'files.maxSize': '📏 最大 {width}×{height}px',
      'files.trueGrayscale': '⚫ True-Grayscale',
      'files.fixSvg': '🔧 修复 SVG',
      'files.autoCrop': '✂ 自动裁边',
      'files.autoCropDesc': '裁剪边框并缩放图片内容以适配',
      'files.targetDevice': '📱 目标设备',
      'files.auto': '自动',
      'files.autoWith': '自动（{device}）',
      'files.rotationDirection': '↻ 转动方向',
      'files.rotationCcw': 'CCW',
      'files.rotationCw': 'CW',
      'files.minOverlap': '📐 最小交错',
      'files.autoDownloadLog': '📥 自动下载记录',
      'files.autoDownloadLogDesc': '下载详细记录和统计信息',
      'files.rememberSettings': '保留上传选项',
      'files.rememberSettingsDesc': '在此浏览器中保存上传选项',
      'files.imageOptions': '🖼️ 图片选项',
      'files.imageCountAllSelectable': '{count} 个图片（全部可选）',
      'files.imageCountSelectable': '{count} 个图片（{selectable} 可选，{locked}）',
      'files.imageCoverLocked': '{width}×{height} - Cover（不可选）',
      'files.imageSeparatorLocked': '{width}×{height} - Separator（不可选）',
      'files.partsCount': '（{count} 个部分）',
      'files.autoCropSize': ' | 自动裁边：{width}×{height}',
      'files.stateNormal': '默认',
      'files.stateHSplit': 'H-Split',
      'files.stateVSplit': 'V-Split',
      'files.stateRotate': '转动',
      'files.legendCover': 'Cover',
      'files.legendSeparator': 'Separator',
      'files.previewFailed': '无法预览图片：{message}\n\n继续转换。',
      'files.jsZipMissing': '没有 JSZip 库。继续转换，无法选择图片。',
      'files.cancel': '取消',
      'files.selectAll': '全选',
      'files.selectItem': '选择 {name}',
      'files.type': '格式',
      'files.sizeColumn': '大小',
      'files.actions': '操作',
      'files.folderBadge': '文件夹',
      'files.deleteFolderTitle': '删除文件夹',
      'files.deleteFileTitle': '删除文件',
      'files.moveFileTitle': '移动文件',
      'files.renameFileTitle': '重命名文件',
      'files.emptyFolder': '文件夹为空',
      'files.loadError': '加载文件时出错',
      'files.folderSummary': '{folders}，{files}，{size}',
      'files.download': '下载',
      'files.imagePreview': '查看图片',
      'files.networkRestored': '网络连接已恢复',
      'files.networkLost': '网络连接已断开',
      'files.uploadCancelled': '上传已取消。',
      'files.allUploadsComplete': '全部文件已上传。',
      'files.uploadedPartial': '已上传 {done}/{total}。失败：{failed}',
      'files.readingMetadata': '正在读取 {file} 的元数据（{index}/{total}）...',
      'files.stageUploading': '正在上传',
      'files.stageConverting': '正在转换并上传',
      'files.uploadingFile': '{stage} {file}（{index}/{total}）{method}',
      'files.uploadingFileProgress': '{stage} {file}（{index}/{total}）{method}：{percent}%',
      'files.convertingFile': '正在转换 {file}（{index}/{total}）...',
      'files.conversionFailedUploadOriginal': '转换失败，正在上传原始文件 {file}...',
      'files.uploadComplete': '上传完成：{file}',
      'files.networkErrorInterrupted': '网络出错，上传被中断',
      'files.selectAtLeastOneFile': '请至少选择一个文件。',
      'files.checkExistingFailed': '无法检查现有文件：{message}',
      'files.selectItemToDelete': '请至少选择一个要删除的项目。',
      'files.deleteWarning': '⚠️ 此操作无法恢复！',
      'files.deleteConfirmText': '确定要删除以下项目？',
      'files.delete': '删除',
      'files.deleteFailed': '删除失败：{message}',
      'files.deleteNetworkError': '删除失败 - 网络出错',
      'files.folderNameRequired': '请输入文件夹名称。',
      'files.folderNameInvalid': '文件夹名称不能包含 " * : < > ? / \\ | ，也不能是 . 或 ..',
      'files.createFolderIn': '在以下位置新建文件夹',
      'files.folderNamePlaceholder': '文件夹名称...',
      'files.createFolder': '创建文件夹',
      'files.createFolderFailed': '创建文件夹失败：{message}',
      'files.createFolderNetworkError': '创建文件夹失败 - 网络出错',
      'files.renameTitle': '重命名文件',
      'files.renaming': '正在重命名',
      'files.newNamePlaceholder': '新文件名称...',
      'files.rename': '重命名',
      'files.newNameRequired': '请输入新名称。',
      'files.nameNoSlashes': '文件名称不能包含 /。',
      'files.renameFailed': '重命名失败：{message}',
      'files.renameNetworkError': '重命名失败 - 网络出错',
      'files.moveTitle': '移动文件',
      'files.moving': '正在移动',
      'files.moveDestPlaceholder': '/目标/文件夹',
      'files.move': '移动',
      'files.moveDestRequired': '请输入目标文件夹。',
      'files.moveFailed': '移动失败：{message}',
      'files.moveNetworkError': '移动失败 - 网络出错',
      'files.conversionLog': '📋 转换记录',
      'files.logStartingBatch': '开始转换 {count} 个文件',
      'files.logSettingsLine': '压缩：{quality}% | 交错：{overlap}% | 转动：{rotation} | True-Grayscale：{grayscale} | 自动裁边：{crop}',
      'files.on': '开启',
      'files.off': '关闭',
      'files.logConversionComplete': '转换完成！',
      'files.logUploadOriginal': '改为上传原始文件...',
      'files.logUploadFailed': '上传失败：{message}',
      'files.logConversionFailed': '转换失败：{message}',
      'files.logGenerated': '生成时间：{date}',
      'files.logImageFailed': '提示：{file} 无法转换，使用原始图片',
      'files.logRemovedFonts': '已移除 {fonts} 个字体文件和 {rules} 个 @font-face 项目，{size} 字体数据',
      'files.logRemovedFontFile': '已移除嵌入字体 {file}（{size}）',
      'files.logPartsCount': '{count} 个部分',
      'files.logSummaryTitle': '📊 转换结果',
      'files.logBatchSummaryTitle': '📊 全部结果',
      'files.logImagesFound': '找到的图片',
      'files.logImagesProcessed': '已转换图片',
      'files.logRepairs': 'EPUB 修复',
      'files.logFixesApplied': '已应用 {count} 个修复',
      'files.logNoneNeeded': '无需修复',
      'files.logErrors': '出错',
      'files.logOriginalSize': '原始大小',
      'files.logOptimizedSize': '转换后大小',
      'files.logSaved': '已压缩',
      'files.logTime': '用时',
      'files.logFilesProcessed': '已转换文件',
      'files.logSuccessful': '成功',
      'files.logFailed': '失败',
      'files.logTotalImages': '全部已转换图片',
      'files.logTotalSplits': '全部分段',
      'files.logTotalFixes': '全部修复',
      'files.logTotalErrors': '全部出错',
      'files.logTotalOriginal': '全部原始大小',
      'files.logTotalOptimized': '全部转换后大小',
      'files.logTotalSaved': '全部已压缩',
      'files.logTotalTime': '全部用时',
      'files.logSplitInto': '分为 {count} 个部分',
      'files.logFixComments': '注释（{count}，{size}）',
      'files.logFixSvgCover': 'SVG Cover',
      'files.logFixSvgImages': 'SVG 图片（{count}）',
      'files.logFixFontFaceRules': '{count} 个项目已从 {file} 移除',
      'files.logFixOpfDetail': 'manifest 已更新',
      'files.logFixNcxDetail': '与 OPF 同步',
      'files.logFixEncryptionDropped': '已移除（包含字体项目）',
      'files.logFixEncryptionObfuscated': '已移除字体项目',
      'settings.docTitle': '设置 - tenor/cross',
      'settings.save': '保存设置',
      'settings.saving': '正在保存...',
      'settings.noChanges': '没有要保存的更改。',
      'settings.saved': '设置已保存。',
      'settings.error': '出错：{message}',
      'settings.loadFailed': '无法加载设置',
      'settings.password': '密码',
      'settings.username': '用户名',
      'settings.serverName': '服务器名称',
      'settings.unchanged': '（保持原值）',
      'settings.lastConnected': '最近连接的网络',
      'settings.wifiTitle': 'Wi-Fi 网络',
      'settings.wifiEmpty': '尚未保存 Wi-Fi 网络',
      'settings.addNetwork': '添加网络',
      'settings.remove': '删除',
      'settings.saveOne': '保存',
      'settings.ssidRequired': '需要 SSID。',
      'settings.wifiSaved': 'Wi-Fi 网络已保存。',
      'settings.wifiDeleted': 'Wi-Fi 网络已删除。',
      'settings.wifiDeleteConfirm': '删除此 Wi-Fi 网络？',
      'settings.opdsTitle': 'OPDS 服务器',
      'settings.opdsEmpty': '尚未配置 OPDS 服务器',
      'settings.addServer': '添加服务器',
      'settings.opdsSaved': 'OPDS 服务器已保存。',
      'settings.opdsDeleted': 'OPDS 服务器已删除。',
      'settings.opdsDeleteConfirm': '删除此 OPDS 服务器？',
      'files.coverCount.other': '{count} 个 Cover',
      'files.separatorCount.other': '{count} 个 Separator',
      'files.folderCount.other': '{count} 个文件夹',
      'files.fileCount.other': '{count} 个文件',
      'files.imageCount.other': '{count} 个图片'
    }
  };

  var current = DEFAULT_LOCALE;
  var initialized = false;
  var stylesInjected = false;
  var bindings = [];
  var listeners = [];
  var missing = [];

  function canonicalLocale(code) {
    if (typeof code !== 'string') return null;
    var trimmed = code.trim();
    if (!trimmed) return null;
    for (var i = 0; i < LOCALES.length; i++) {
      if (LOCALES[i] === trimmed) return LOCALES[i];
    }
    var lowered = trimmed.toLowerCase();
    for (var j = 0; j < LOCALES.length; j++) {
      if (LOCALES[j].toLowerCase() === lowered) return LOCALES[j];
    }
    return null;
  }

  function storedLocale() {
    try {
      if (!window.localStorage) return null;
      return canonicalLocale(window.localStorage.getItem(STORAGE_KEY));
    } catch (e) {
      // Private mode or a full/blocked storage: fall back to the request.
      return null;
    }
  }

  function paramLocale() {
    try {
      if (!window.location || typeof URLSearchParams === 'undefined') return null;
      return canonicalLocale(new URLSearchParams(window.location.search).get(LANG_PARAM));
    } catch (e) {
      return null;
    }
  }

  function persistLocale(code) {
    try {
      if (!window.localStorage) return false;
      window.localStorage.setItem(STORAGE_KEY, code);
      return true;
    } catch (e) {
      // The page keeps `current`; the choice still travels in ?lang=.
      return false;
    }
  }

  function messageFor(locale, key) {
    var table = CATALOGUES[locale];
    if (!table) return null;
    var value = table[key];
    return typeof value === 'string' ? value : null;
  }

  function lookup(key) {
    var value = messageFor(current, key);
    if (value === null) value = messageFor(DEFAULT_LOCALE, key);
    return value;
  }

  function recordMissing(key) {
    if (missing.indexOf(key) === -1) missing.push(key);
  }

  function interpolate(template, params) {
    return template.replace(/\{(\w+)\}/g, function (whole, name) {
      if (!params) return whole;
      var value = params[name];
      if (typeof value === 'function') value = value();
      if (value === undefined || value === null) return whole;
      return String(value);
    });
  }

  function t(key, params) {
    ensureInit();
    var value = lookup(key);
    if (value === null) {
      recordMissing(key);
      return key;
    }
    return interpolate(value, params);
  }

  function tPlural(baseKey, count, params) {
    ensureInit();
    var suffix = Number(count) === 1 ? '.one' : '.other';
    var merged = {};
    var name;
    if (params) {
      for (name in params) {
        if (Object.prototype.hasOwnProperty.call(params, name)) merged[name] = params[name];
      }
    }
    merged.count = count;
    var value = messageFor(current, baseKey + suffix);
    if (value === null) value = messageFor(current, baseKey + '.other');
    if (value === null) value = messageFor(DEFAULT_LOCALE, baseKey + suffix);
    if (value === null) value = messageFor(DEFAULT_LOCALE, baseKey + '.other');
    if (value === null) value = lookup(baseKey);
    if (value === null) {
      recordMissing(baseKey);
      return baseKey;
    }
    return interpolate(value, merged);
  }

  function withLang(url) {
    ensureInit();
    if (typeof url !== 'string' || url === '') return url;
    if (url.charAt(0) === '#') return url;
    if (typeof window === 'undefined' || !window.location) return url;
    var page = window.location;
    var resolved;
    try {
      resolved = new URL(url, page.href);
    } catch (e) {
      return url;
    }
    // Only same-origin http(s) targets carry the locale. External hosts,
    // mailto:/tel:/data: and other schemes are returned untouched.
    if ((resolved.protocol !== 'http:' && resolved.protocol !== 'https:') || resolved.origin !== page.origin) {
      return url;
    }
    // Rewrite the query textually: round-tripping through URLSearchParams would
    // re-encode an existing query (spaces as '+', slashes as '%2F') that other
    // handlers and the device's own arg parsing expect verbatim.
    var hash = '';
    var hashIndex = url.indexOf('#');
    if (hashIndex !== -1) {
      hash = url.slice(hashIndex);
      url = url.slice(0, hashIndex);
    }
    var query = '';
    var queryIndex = url.indexOf('?');
    if (queryIndex !== -1) {
      query = url.slice(queryIndex + 1);
      url = url.slice(0, queryIndex);
    }
    var parts = [];
    if (query) {
      var existing = query.split('&');
      for (var i = 0; i < existing.length; i++) {
        if (existing[i] && !/^lang=/i.test(existing[i])) parts.push(existing[i]);
      }
    }
    parts.push(LANG_PARAM + '=' + encodeURIComponent(current));
    return url + '?' + parts.join('&') + hash;
  }

  function formatNumber(value) {
    ensureInit();
    try {
      return Number(value).toLocaleString(current);
    } catch (e) {
      return String(value);
    }
  }

  function translateElements(scope) {
    if (!scope || !scope.querySelectorAll) return;
    var nodes = scope.querySelectorAll('[data-i18n]');
    for (var i = 0; i < nodes.length; i++) {
      nodes[i].textContent = t(nodes[i].getAttribute('data-i18n'));
    }
    var attrs = ['title', 'aria-label', 'placeholder'];
    for (var a = 0; a < attrs.length; a++) {
      var marker = 'data-i18n-' + attrs[a];
      var holders = scope.querySelectorAll('[' + marker + ']');
      for (var h = 0; h < holders.length; h++) {
        holders[h].setAttribute(attrs[a], t(holders[h].getAttribute(marker)));
      }
    }
  }

  function applyBindings(scope) {
    if (!scope || !scope.querySelectorAll) return;
    for (var i = 0; i < bindings.length; i++) {
      var nodes = scope.querySelectorAll(bindings[i].selector);
      for (var k = 0; k < nodes.length; k++) {
        if (bindings[i].attr) nodes[k].setAttribute(bindings[i].attr, t(bindings[i].key));
        else nodes[k].textContent = t(bindings[i].key);
      }
    }
  }

  function localizeAnchors(scope) {
    if (!scope || !scope.querySelectorAll) return;
    var anchors = scope.querySelectorAll('a[href]');
    for (var i = 0; i < anchors.length; i++) {
      var href = anchors[i].getAttribute('href');
      var localized = withLang(href);
      if (localized !== href) anchors[i].setAttribute('href', localized);
    }
  }

  function injectStyles() {
    if (stylesInjected || typeof document === 'undefined' || !document.head) return;
    stylesInjected = true;
    var style = document.createElement('style');
    style.id = 'tenor-i18n-style';
    style.textContent =
      '.tenor-lang-slot{display:flex;align-items:center;gap:6px;align-self:center;margin-left:auto;font-size:0.9em}' +
      '.tenor-lang-slot label{color:var(--label-color)}' +
      '.tenor-lang-slot select{padding:6px 8px;border:1px solid var(--border-color);border-radius:4px;background:var(--card-bg);color:var(--font-color)}';
    document.head.appendChild(style);
  }

  function mountSelector(host) {
    ensureInit();
    if (!host || typeof document === 'undefined') return null;
    injectStyles();
    var label = document.createElement('label');
    label.className = LABEL_CLASS;
    label.setAttribute('for', 'tenor-web-language');
    label.textContent = t('lang.label');
    var select = document.createElement('select');
    select.className = SELECT_CLASS;
    select.id = 'tenor-web-language';
    for (var i = 0; i < OPTIONS.length; i++) {
      var option = document.createElement('option');
      option.value = OPTIONS[i].code;
      option.textContent = OPTIONS[i].label;
      select.appendChild(option);
    }
    select.value = current;
    select.addEventListener('change', function () {
      setLocale(select.value);
    });
    host.replaceChildren(label, select);
    return select;
  }

  function mountAllSelectors() {
    if (typeof document === 'undefined') return;
    var hosts = document.querySelectorAll('[' + SELECTOR_ATTR + ']');
    for (var i = 0; i < hosts.length; i++) {
      if (!hosts[i].querySelector('select.' + SELECT_CLASS)) mountSelector(hosts[i]);
    }
  }

  function syncSelectors() {
    if (typeof document === 'undefined') return;
    var selects = document.querySelectorAll('select.' + SELECT_CLASS);
    for (var i = 0; i < selects.length; i++) {
      selects[i].value = current;
    }
    var labels = document.querySelectorAll('label.' + LABEL_CLASS);
    for (var k = 0; k < labels.length; k++) {
      labels[k].textContent = t('lang.label');
    }
  }

  function syncLangAttribute() {
    if (typeof document !== 'undefined' && document.documentElement) {
      document.documentElement.lang = current;
    }
  }

  function apply(root) {
    ensureInit();
    if (typeof document === 'undefined') return;
    syncLangAttribute();
    translateElements(root || document);
    mountAllSelectors();
    syncSelectors();
    applyBindings(document);
    localizeAnchors(document);
  }

  function notify() {
    var snapshot = listeners.slice();
    for (var i = 0; i < snapshot.length; i++) {
      try {
        snapshot[i](current);
      } catch (e) {
        if (typeof console !== 'undefined') console.error('tenor i18n listener failed', e);
      }
    }
  }

  function init() {
    if (initialized) return current;
    initialized = true;
    current = storedLocale() || paramLocale() || DEFAULT_LOCALE;
    if (typeof document !== 'undefined') apply(document);
    return current;
  }

  function ensureInit() {
    if (!initialized) init();
  }

  function locale() {
    ensureInit();
    return current;
  }

  function setLocale(code) {
    ensureInit();
    var next = canonicalLocale(code);
    if (next === null || next === current) return current;
    current = next;
    persistLocale(next);
    apply(document);
    notify();
    return current;
  }

  function onChange(listener) {
    if (typeof listener !== 'function') return function () {};
    listeners.push(listener);
    return function () {
      var index = listeners.indexOf(listener);
      if (index !== -1) listeners.splice(index, 1);
    };
  }

  function bind(selector, key, attr) {
    ensureInit();
    bindings.push({ selector: selector, key: key, attr: attr || null });
    // init() already ran its apply pass before this binding existed, so a page
    // that binds after init would otherwise stay untranslated until the next
    // locale change.
    applyBindings(document);
    return bindings.length;
  }

  return {
    LOCALES: LOCALES,
    DEFAULT_LOCALE: DEFAULT_LOCALE,
    STORAGE_KEY: STORAGE_KEY,
    LANG_PARAM: LANG_PARAM,
    OPTIONS: OPTIONS,
    missing: missing,
    init: init,
    apply: apply,
    t: t,
    tPlural: tPlural,
    locale: locale,
    setLocale: setLocale,
    onChange: onChange,
    withLang: withLang,
    apiUrl: withLang,
    bind: bind,
    mountSelector: mountSelector,
    formatNumber: formatNumber
  };
})();
