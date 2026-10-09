def extract_pnum_subs(font_path):
    """Extract the proportional figure substitutions used by builtin fonts."""
    from fontTools.ttLib import TTFont

    with TTFont(font_path) as font:
        if 'GSUB' not in font:
            return {}
        gsub = font['GSUB'].table
        lookup_indices = set()
        if gsub.FeatureList:
            for feature in gsub.FeatureList.FeatureRecord:
                if feature.FeatureTag == 'pnum':
                    lookup_indices.update(feature.Feature.LookupListIndex)
        substitutions = {}
        for lookup_index in sorted(lookup_indices):
            lookup = gsub.LookupList.Lookup[lookup_index]
            for subtable in lookup.SubTable:
                actual = subtable
                if lookup.LookupType == 7 and hasattr(subtable, 'ExtSubTable'):
                    actual = subtable.ExtSubTable
                if hasattr(actual, 'mapping'):
                    substitutions.update(actual.mapping)
        return substitutions


def pnum_glyph_indices(font_path):
    from fontTools.ttLib import TTFont

    substitutions = extract_pnum_subs(font_path)
    with TTFont(font_path) as font:
        indices = {name: index for index, name in enumerate(font.getGlyphOrder())}
        return {codepoint: indices[substitutions[name]]
                for codepoint, name in (font.getBestCmap() or {}).items()
                if name in substitutions and indices.get(substitutions[name], 0) > 0}
