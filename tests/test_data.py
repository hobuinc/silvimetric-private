from silvimetric import Data, Bounds
from silvimetric.resources.config import StorageConfig


class Test_Data(object):  # noqa: D101
    def test_ept_reader_ignores_unreadable_tiles(self):
        """EPT source transport errors must not abort an entire shatter run."""

        class Reader:
            type = 'readers.ept'

            def __init__(self):
                self._options = {}

        reader = Reader()
        Data._apply_ept_options(reader)

        assert reader._options['ignore_unreadable'] is True

    def test_tindex_copc_reader_args_receive_bounds_and_resolution(self):
        """A tindex planner query must constrain its embedded COPC readers."""
        bounds = Bounds(1, 2, 3, 4)
        args = Data._tindex_reader_args(
            [
                {'type': 'readers.las', 'nosrs': True},
                {'type': 'readers.copc', 'requests': 4},
            ],
            bounds,
            160,
        )
        copc = next(arg for arg in args if arg['type'] == 'readers.copc')
        assert copc == {
            'type': 'readers.copc',
            'requests': 4,
            'bounds': str(bounds),
            'resolution': 160,
        }
        assert args[0] == {'type': 'readers.las', 'nosrs': True}

    def test_tindex_copc_reader_args_are_created_when_absent(self):
        """COPC options are safe to supply even if the index selects LAS too."""
        args = Data._tindex_reader_args(None, Bounds(1, 2, 3, 4), 160)
        assert args == [
            {
                'type': 'readers.copc',
                'bounds': '[1.0, 2.0, 3.0, 4.0]',
                'resolution': 160,
            }
        ]

    def test_filepath(
        self,
        no_cell_line_path: str,
        storage_config: StorageConfig,
        no_cell_line_pc: int,
        bounds: Bounds,
    ):
        """Check open a COPC file"""
        data = Data(no_cell_line_path, storage_config)
        assert not data.is_pipeline()
        data.execute()
        assert len(data.array) == no_cell_line_pc
        assert data.estimate_count(bounds) == no_cell_line_pc

    def test_pipeline(
        self,
        no_cell_line_pipeline: str,
        bounds: Bounds,
        storage_config: StorageConfig,
        no_cell_line_pc: int,
    ):
        """Check open a pipeline"""
        data = Data(no_cell_line_pipeline, storage_config)
        assert data.is_pipeline()
        data.execute()
        assert len(data.array) == no_cell_line_pc
        assert data.estimate_count(bounds) == no_cell_line_pc

    def test_pipeline_bounds(
        self,
        no_cell_line_pipeline: str,
        bounds: Bounds,
        storage_config: StorageConfig,
        no_cell_line_pc: int,
    ):
        """Check open a pipeline with our own bounds"""
        ll = next(iter(bounds.bisect()))

        data = Data(no_cell_line_pipeline, storage_config, bounds=ll)

        # data will be collared upon execution, extra data will be grabbed

        minx, miny, maxx, maxy = data.bounds.get()
        collared = Bounds(minx - 30, miny - 30, maxx + 30, maxy + 30)

        assert data.is_pipeline()
        data.execute()

        collared_count = data.count(collared)
        assert len(data.array) == collared_count

        assert data.estimate_count(ll) == no_cell_line_pc
        correct_count = (
            21025
            if storage_config.alignment.lower() == 'aligntocorner'
            else 25600
        )
        assert data.count(ll) == correct_count


class Test_Autzen(object):  # noqa: D101
    def test_filepath(
        self, autzen_filepath: str, storage_config: StorageConfig
    ):
        """Check open Autzen"""
        data = Data(autzen_filepath, storage_config)
        assert not data.is_pipeline()
        data.execute()
        assert len(data.array) == 577637
        assert data.estimate_count(data.bounds) == 577637
