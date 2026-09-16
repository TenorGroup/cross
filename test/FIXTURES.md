# Test fixture provenance

Host tests use short strings and state constructed within the test sources. Hyphenation evaluation uses 96 independently selected technical words across eight languages, with uniform weights and Pyphen 0.17.2 annotations. `hyphenation_eval/resources/generate_synthetic.py` reproduces those fixtures. These samples replace a literary corpus; the original 40,000-word coverage is not claimed.

Binary EPUBs and screenshots with uncertain provenance are excluded. The `scripts/generate_*_epub.py` scripts construct diagnostic documents locally from synthetic markup. Generated books, device caches and screenshots are ignored by Git.

The Spanish sample exposes a pre-existing limitation for computadora: the engine returns compu=tado=ra, while Pyphen gives com=pu=ta=do=ra. The sample is retained. Its new-corpus baseline is 97.22%, and the threshold is 96.20%, following the existing one-point tolerance. Firmware behaviour is unchanged.
