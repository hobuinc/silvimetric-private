import numpy as np
import pandas as pd

from silvimetric import shatter, Storage, Metric
from silvimetric import all_metrics as s
from silvimetric import l_moments
from silvimetric.resources.attribute import Attribute
from silvimetric.resources.taskgraph import Graph
from silvimetric.resources.attribute import Pdal_Attributes as dims
from silvimetric.resources.metrics.l_moments import lmom4

from test_shatter import confirm_one_entry


class TestMetrics:
    def test_l_moments_do_not_overflow_dense_cells(self):
        values = pd.Series(np.full(60_000, 7.0))
        l1, l2, l3, l4 = lmom4(values)
        assert np.isclose(l1, 7.0)
        assert np.allclose([l2, l3, l4], 0.0, atol=1e-10)

    def test_l_moments_dense_match_arbitrary_precision_reference(self):
        values = np.arange(60_000, dtype=np.int64) % 97
        n = len(values)
        descending = sorted(values, reverse=True)
        b0 = sum(int(value) for value in descending) / n
        b1 = sum(
            int(value) * (n - index - 1)
            for index, value in enumerate(descending)
        ) / (n * (n - 1))
        b2 = sum(
            int(value) * (n - index - 1) * (n - index - 2)
            for index, value in enumerate(descending[:-1])
        ) / (n * (n - 1) * (n - 2))
        b3 = sum(
            int(value) * (n - index - 1) * (n - index - 2)
            * (n - index - 3)
            for index, value in enumerate(descending[:-2])
        ) / (n * (n - 1) * (n - 2) * (n - 3))
        expected = (
            b0,
            2 * b1 - b0,
            6 * (b2 - b1) + b0,
            20 * b3 - 30 * b2 + 12 * b1 - b0,
        )
        assert np.allclose(lmom4(pd.Series(values)), expected, atol=1e-10)

    def test_dag(
        self, metric_data: pd.DataFrame, metric_dag_results: pd.DataFrame
    ):
        metrics = list(l_moments.values())
        g = Graph(metrics).init()
        assert g.initialized
        assert all(n.initialized for n in g.nodes.values())
        assert not set(g.nodes.keys()) ^ set([
            'l1',
            'l2',
            'l3',
            'l4',
            'lcv',
            'lskewness',
            'lkurtosis',
            'lmombase',
        ])

        g.run(metric_data)
        for node in g.nodes.values():
            assert isinstance(node.results, pd.DataFrame)
            assert all(node.results.any())

        assert all(g.results == metric_dag_results)

    def test_metrics(
        self,
        metric_data: pd.DataFrame,
        metric_data_results: pd.DataFrame,
    ):
        ms = list(s.values())
        graph = Graph(ms)
        metrics = graph.run(metric_data)
        assert isinstance(metrics, pd.DataFrame)
        # This fixture predates the FUSION sample-SD correction. Those eight
        # product-moment outputs have a dedicated FUSION-equivalence test;
        # retain this historical snapshot for every unchanged metric.
        fusion_corrected = {
            'm_Z_stddev',
            'm_Intensity_stddev',
            'm_Z_cv',
            'm_Intensity_cv',
            'm_Z_skewness',
            'm_Intensity_skewness',
            'm_Z_kurtosis',
            'm_Intensity_kurtosis',
        }
        for m in metric_data_results.columns:
            if m not in fusion_corrected:
                assert all(
                    np.isclose(metric_data_results[m].values, metrics[m].values)
                )

    def test_dependencies(self, metric_data: pd.DataFrame):
        # should be able to create a dependency graph
        cv = s['cv']
        mean = s['mean']
        stddev = s['stddev']

        cv.dependencies = [mean, stddev]

        b = Graph([cv]).run(metric_data)
        # cv/mean should be there
        assert b.m_Z_cv.any()

        # and median/stddev should not
        assert not any(x in b.dtypes for x in ['m_Z_median', 'm_Z_stddev'])

    def test_filter(
        self, metric_shatter_config: pd.Series, test_point_count: int
    ):
        pc = shatter(metric_shatter_config)
        assert pc == test_point_count

        s = Storage.from_db(metric_shatter_config.tdb_dir)
        m = s.config.metrics[0]
        assert len(m.filters) == 1

        base = 11 if s.config.alignment == 'AlignToCenter' else 10
        maxy = s.config.root.maxy
        confirm_one_entry(s, maxy, base, test_point_count)

    def test_custom(
        self, metric_data: pd.DataFrame, attrs: list[Attribute]
    ) -> None:
        def m_over500(data):
            return data[data >= 500].count()

        z_att = attrs[0]
        m_cust = Metric(
            name='over500',
            dtype=np.float32,
            method=m_over500,
            attributes=[z_att],
        )

        b = Graph(m_cust).init().run(metric_data)

        assert b.m_Z_over500.any()
        assert b.m_Z_over500.values[0] == 2

    def test_dependency_passing(
        self, dep_crr: Metric, depless_crr: Metric, metric_data: pd.DataFrame
    ):
        nd1 = Graph(depless_crr).init().run(metric_data)
        nd2 = Graph(dep_crr).init().run(metric_data)
        assert all(nd2.m_Z_deps_crr == nd1.m_Z_depless_crr)
