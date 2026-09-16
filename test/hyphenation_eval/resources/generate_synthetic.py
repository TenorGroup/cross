"""Generate independent vocabulary fixtures with Pyphen 0.17.2 (pip install pyphen==0.17.2)."""
from pathlib import Path
import pyphen
WORDS = {
 'english': ('en_US', 3, 3, 'computer reading library navigation dictionary development configuration typography generation information paragraph character'),
 'french': ('fr_FR', 2, 2, 'ordinateur lecture bibliothèque navigation dictionnaire développement configuration typographie génération information paragraphe caractère'),
 'german': ('de_DE', 2, 2, 'Computer Lesegerät Bibliothek Navigation Wörterbuch Entwicklung Konfiguration Typografie Information Absatz Bildschirm Tastatur'),
 'russian': ('ru_RU', 2, 2, 'компьютер чтение библиотека навигация словарь разработка конфигурация типография информация параграф символ клавиатура'),
 'spanish': ('es_ES', 2, 2, 'computadora lectura biblioteca navegación diccionario desarrollo configuración tipografía generación información párrafo carácter'),
 'italian': ('it_IT', 2, 2, 'computer lettura biblioteca navigazione dizionario sviluppo configurazione tipografia generazione informazione paragrafo carattere'),
 'polish': ('pl_PL', 2, 2, 'komputer czytanie biblioteka nawigacja słownik rozwój konfiguracja typografia informacja paragraf klawiatura ekran'),
 'swedish': ('sv_SE', 2, 2, 'dator läsning bibliotek navigering ordbok utveckling konfiguration typografi information paragraf tangentbord bildskärm'),
}
for language, (code, left, right, words) in WORDS.items():
 d = pyphen.Pyphen(lang=code, left=left, right=right)
 lines = ['# Independently selected technical vocabulary. No book text or frequency data.', '# Pyphen 0.17.2 annotations; word|hyphenation|uniform weight']
 lines += [f'{word}|{d.inserted(word, "=")}|1' for word in words.split()]
 Path(__file__).with_name(language + '_hyphenation_tests.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
