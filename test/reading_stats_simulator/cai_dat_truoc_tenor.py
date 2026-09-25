"""Settings a fixture wrote before the tenor/cross reading setup (v1.0.14).

A settings.json without `tenorPresetVersion` is moved onto the setup on its first
load (CrossPointSettings::applyTenorPreset), and a key the file leaves out now
starts from the setup too. Fixtures written against the earlier defaults wrap
their settings in truoc_tenor() so they keep checking what they were written for;
the move itself is checked by test/tenor_preset.
"""


def truoc_tenor(settings=None):
    """The fourteen setup keys at their earlier defaults, the stamp, then the fixture's own keys."""
    settings = dict(settings or {})
    # Files without textSpacingVersion 3 have their line spacing folded from the old
    # three-value row, where 1 was the default.
    old_line = 0 if settings.get('textSpacingVersion', 0) >= 3 else 1
    base = {
        'tenorPresetVersion': 1,
        'extraParagraphSpacing': 0,
        'lineSpacing': old_line,
        'wordSpacing': 0,
        'paragraphIndent': 1,
        'readerInkWeight': 0,
        'frontButtonFollowOrientation': 0,
        'longPressMenuFunction': 1,
        'shortPwrBtn': 0,
        'sleepScreen': 8,
        'statusBarClock': 0,
        'tiltMenuNavigation': 0,
        'tiltPageTurn': 0,
        'tiltStrengthV': 1,
        'tiltTabNavigation': 0,
    }
    base.update(settings)
    return base
