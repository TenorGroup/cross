<footer class="tenor-footer"><span data-i18n="home.footerLibrary">Thư viện trên thiết bị</span><a href="https://cross.tenor.vn" target="_blank" rel="noopener noreferrer">cross.tenor.vn ↗</a></footer>
<dialog id="webMessageDialog" class="web-dialog" aria-labelledby="webDialogTitle" aria-describedby="webDialogMessage">
  <form method="dialog">
    <h2 id="webDialogTitle"></h2>
    <p id="webDialogMessage"></p>
    <div class="web-dialog-actions">
      <button id="webDialogCancel" value="cancel" data-i18n="common.cancel">Hủy</button>
      <button id="webDialogConfirm" value="confirm">OK</button>
    </div>
  </form>
</dialog>
<div id="webToast" class="toast" role="status" aria-live="polite"></div>
<script src="/js/web-i18n.js"></script>
<script>
// Mark the nav entry of the page being shown (the same header serves every page).
document.querySelectorAll('.nav-links a').forEach(a => {
  if (a.pathname === location.pathname) a.classList.add('active');
});

const webMessageDialog = document.getElementById('webMessageDialog');
const webDialogTitle = document.getElementById('webDialogTitle');
const webDialogMessage = document.getElementById('webDialogMessage');
const webDialogCancel = document.getElementById('webDialogCancel');
const webDialogConfirm = document.getElementById('webDialogConfirm');
let webDialogQueue = Promise.resolve();

function showWebDialog(title, message, confirm) {
  const result = webDialogQueue.then(() => new Promise(resolve => {
    webDialogTitle.textContent = title;
    webDialogMessage.textContent = String(message);
    webDialogCancel.hidden = !confirm;
    webDialogConfirm.textContent = TenorI18n.t(confirm ? 'common.confirm' : 'common.ok');
    webMessageDialog.returnValue = '';
    webMessageDialog.addEventListener('close', () => {
      resolve(webMessageDialog.returnValue === 'confirm');
    }, { once: true });
    webMessageDialog.showModal();
  }));
  webDialogQueue = result.then(() => undefined, () => undefined);
  return result;
}

function showWebAlert(message) {
  return showWebDialog(TenorI18n.t('common.notice'), message, false).then(() => undefined);
}

function showWebConfirm(message) {
  return showWebDialog(TenorI18n.t('common.confirmAction'), message, true);
}

const webToast = document.getElementById('webToast');
let webToastTimeout;

function showWebToast(message, isError) {
  clearTimeout(webToastTimeout);
  webToast.textContent = message;
  webToast.className = 'toast' + (isError ? ' error' : '');
  webToast.style.display = 'block';
  webToastTimeout = setTimeout(() => { webToast.style.display = 'none'; }, 4000);
}
</script>
