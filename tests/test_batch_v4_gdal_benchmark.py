"""Named TileDB attribute VRT construction must not lose its source path."""

from xml.etree import ElementTree as ET

import pytest

from tools.benchmark_batch_v4_gdal import (
    fix_vrt_source,
    named_attribute_uri,
    parse_window,
)


def test_named_attribute_vrt_source_is_preserved():
    name = named_attribute_uri('s3://example/db/becker', 'm_Z_mean')
    assert name == 'TILEDB:/vsis3/example/db/becker:m_Z_mean'
    xml = '''<VRTDataset rasterXSize="10" rasterYSize="10">
      <VRTRasterBand dataType="Float32" band="1"><SimpleSource>
      <SourceFilename relativeToVRT="0">s3://example/db/becker</SourceFilename>
      <SourceBand>1</SourceBand></SimpleSource></VRTRasterBand></VRTDataset>'''
    corrected = ET.fromstring(fix_vrt_source(xml, name))
    source = corrected.find('.//SourceFilename')
    assert source.text == name
    assert source.get('relativeToVRT') == '0'


def test_fusion_strata_attribute_name_is_accepted_without_uri_injection():
    assert named_attribute_uri('s3://example/db/becker', 'm_Z_Strata-1') == (
        'TILEDB:/vsis3/example/db/becker:m_Z_Strata-1'
    )
    for invalid in ('m_Z_Strata-1:other', 'm_Z/Strata-1', '../count', ''):
        with pytest.raises(ValueError, match='Invalid TileDB attribute'):
            named_attribute_uri('s3://example/db/becker', invalid)


def test_vrt_rejects_ambiguous_source_and_invalid_windows():
    with pytest.raises(RuntimeError, match='Expected one VRT source'):
        fix_vrt_source('<VRTDataset/>', 'TILEDB:/vsis3/bucket/db:count')
    with pytest.raises(ValueError):
        named_attribute_uri('s3://example/db', '../count')
    assert parse_window('dense:1:2:8:16') == ('dense', (1, 2, 8, 16))
    with pytest.raises(ValueError):
        parse_window('bad:0:0:0:8')
