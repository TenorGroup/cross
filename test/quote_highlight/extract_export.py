from pathlib import Path
import sys

root, output = Path(sys.argv[1]), Path(sys.argv[2])
source = (root / 'src/network/CrossPointWebServer.cpp').read_text()
start = source.index('void CrossPointWebServer::handleQuotesExport()')
end = source.index('// Diagnostic counters', start)
route = source[source.index('server->on("/api/quotes/export"'):].split('});', 1)[0]
assert 'HTTP_GET' in route
assert 'if (auth.authorize(*server, true, requestLanguage())) handleQuotesExport();' in route
page = (root / 'src/network/html/FilesPage.html').read_text()
assert page.count('href="/api/quotes/export"') == 1
assert 'data-i18n="files.exportQuotes"' in page
# The link wears the same look as the buttons beside it (theme rules name a.action-btn).
theme = (root / 'src/network/html/Theme.css').read_text()
assert 'a.action-btn{text-decoration:none}' in theme and 'a.action-btn{grid-column:1/-1' in theme
output.write_text(source[start:end])
