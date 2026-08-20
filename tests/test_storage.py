import tiledb
import numpy as np
import pytest
import os
import copy

from silvimetric import (
    Storage,
    Bounds,
    all_metrics,
    Attribute,
    Attributes,
    StorageConfig,
)
from silvimetric import __version__ as svversion
from silvimetric.resources.config import ShatterConfig


class Test_Storage(object):
    def test_schema(self, storage: Storage, attrs: list[Attribute]):
        with storage.open('r') as st:
            s: tiledb.ArraySchema = st.schema
            assert s.has_attr('count')
            assert s.attr('count').dtype == np.uint32

            for a in attrs:
                assert s.has_attr(a.name)
                # assert s.attr(a.name) == a.schema()

    def test_time_reserve(self, storage):
        for x in range(5):
            time_slot = storage.reserve_time_slot()
            assert time_slot == x + 1

    def test_local(self, storage: Storage, attrs: list[Attribute]):
        with storage.open('r') as st:
            sc = st.schema
            assert sc.has_attr('shatter_process_num')
            assert sc.attr('shatter_process_num').dtype == np.uint16
            assert sc.has_attr('count')
            assert sc.attr('count').dtype == np.uint32

            for a in attrs:
                assert sc.has_attr(a.name)
                # assert sc.attr(a.name) == a.schema()

    def test_config(self, storage: Storage):
        """Check that instantiation metadata is properly written"""

        storage.save_config()
        config = storage.get_config()
        assert config.resolution == storage.config.resolution
        assert config.alignment == storage.config.alignment
        assert config.root == storage.config.root
        assert config.crs == storage.config.crs
        assert storage.config.version == svversion

    def test_metric_dependencies(
        self,
        tmp_path_factory,
        metrics,
        crs,
        resolution,
        alignment,
        attrs,
        bounds,
    ):
        ms = copy.deepcopy(metrics)

        path = tmp_path_factory.mktemp('test_tdb')
        p = os.path.abspath(path)

        ms[0].dependencies = [Attributes['HeightAboveGround']]
        sc = StorageConfig(
            tdb_dir=p,
            crs=crs,
            resolution=resolution,
            alignment=alignment,
            attrs=attrs,
            metrics=ms,
            root=bounds,
            xsize=5,
            ysize=5
        )

        s = Storage.create(sc)
        assert isinstance(s, Storage)
        assert 'HeightAboveGround' in {attribute.name for attribute in sc.attrs}

        ms[0].dependencies = []

    def test_oob_storage(
        self,
        tmp_path_factory,
        metrics,
        crs,
        resolution,
        attrs,
        bounds,
    ):
        ms = copy.deepcopy(metrics)
        path = tmp_path_factory.mktemp('test_tdb')
        p = os.path.abspath(path)

        sc = StorageConfig(
            tdb_dir=p,
            crs=crs,
            resolution=resolution,
            attrs=attrs,
            metrics=ms,
            root=bounds,
            xsize=1000,
            ysize=1000
        )

        s = Storage.create(sc)
        a = s.open('r')

        assert a.schema.domain.dim('X').tile == 12
        assert s.config.xsize == 12
        assert a.schema.domain.dim('Y').tile == 12
        assert s.config.ysize == 12

    def test_gdal_edge_blocks_are_physically_padded(self, tmp_path, crs):
        """Dense domains include a final full GDAL/TileDB block for reads."""
        storage = Storage.create(
            StorageConfig(
                tdb_dir=str(tmp_path / 'gdal-edge.tdb'),
                crs=crs,
                resolution=20,
                root=Bounds(0, 0, 18020, 18020),
                xsize=256,
                ysize=256,
            )
        )
        with storage.open('r') as array:
            assert array.schema.domain.dim('X').domain == (0, 1023)
            assert array.schema.domain.dim('Y').domain == (0, 1023)


    def test_metrics(self, storage: Storage):
        m_list = storage.get_metrics()
        a_list = storage.get_attributes()

        with storage.open('r') as st:
            s: tiledb.ArraySchema = st.schema
            for m in m_list:
                assert m.name in all_metrics.keys()

                def e_name(att, met):
                    return s.attr(met.entry_name(att.name))

                def schema(att, met):
                    return all_metrics[met.name].schema(att)

                assert all([e_name(a, m) == schema(a, m) for a in a_list])

    def test_metadata(self, storage: Storage, shatter_config: ShatterConfig):
        with storage.open('r') as array:
            gdal_metadata, metadata_type = array.meta.__getitem__(
                '_gdal', include_type=True
            )
            assert metadata_type == tiledb.libtiledb.DataType.UINT8
            assert gdal_metadata.dtype == np.uint8
            assert gdal_metadata.tobytes().decode('utf-8').startswith(
                '<PAMDataset>'
            )

        shatter_config.time_slot = storage.reserve_time_slot()
        storage.save_shatter_meta(shatter_config)
        shc_copy = copy.deepcopy(shatter_config)
        shc_copy.time_slot = storage.reserve_time_slot()
        storage.save_shatter_meta(shc_copy)

        sh_c = storage.get_shatter_meta(shatter_config.time_slot)
        assert sh_c == shatter_config
        sh_c_2 = storage.get_shatter_meta(shc_copy.time_slot)
        assert sh_c_2 == shc_copy

        history = storage.get_history()
        assert len(history) == 2

        assert ShatterConfig.from_json(history[0])== shatter_config
        assert ShatterConfig.from_json(history[1]) == shc_copy
