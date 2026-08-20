import numpy as np

from ..metric import Metric
from ..attribute import Attributes as A
from .p_moments import product_moments
from .stats import statistics


##### Methods #####
def count_fn(data, *args):
    return data.count()


def count_above_mean(data, *args):
    mean = args[0]
    return data[data > mean].count()


def count_above_mode(data, *args):
    mode = args[0]
    return data[data > mode].count()


def first_returns_filter(data):
    return data[data.ReturnNumber == 1]


def make_returns_filter(val):
    def returns_filter(data):
        return data[data.ReturnNumber == val]

    return returns_filter


def second_returns_filter(data):
    return data[data.ReturnNumber == 2]


def third_returns_filter(data):
    return data[data.ReturnNumber == 3]


def other_returns_filter(data):
    """FUSION's ``rothercount``: return numbers outside 1 through 9."""
    return data[(data.ReturnNumber <= 0) | (data.ReturnNumber >= 10)]


##### Counts ######
def get_count_metrics(elev_key='Z'):
    all_count = Metric(
        'all_count',
        np.int32,
        count_fn,
        attributes=[A['ReturnNumber']],
    )
    all_count_above_minht = Metric(
        'all_count_above_minht',
        np.int32,
        count_fn,
        attributes=[A['ReturnNumber']],
    )
    all_count_above_htbreak = Metric(
        'all_count_above_htbreak',
        np.int32,
        count_fn,
        attributes=[A['ReturnNumber']],
    )
    first_count_above_htbreak = Metric(
        '1st_count_above_htbreak',
        np.int32,
        count_fn,
        filters=[first_returns_filter],
        attributes=[A['ReturnNumber']],
    )
    all_count_above_mean = Metric(
        'all_count_above_mean',
        np.int32,
        count_above_mean,
        attributes=[A[elev_key]],
        dependencies=[product_moments['mean']],
    )
    all_count_above_mode = Metric(
        'all_count_above_mode',
        np.int32,
        count_above_mode,
        attributes=[A[elev_key]],
        dependencies=[statistics['mode']],
    )
    first_count_above_mean = Metric(
        '1st_count_above_mean',
        np.int32,
        count_above_mean,
        filters=[first_returns_filter],
        attributes=[A[elev_key]],
        dependencies=[product_moments['mean']],
    )
    first_count_above_mode = Metric(
        '1st_count_above_mode',
        np.int32,
        count_above_mode,
        filters=[first_returns_filter],
        attributes=[A[elev_key]],
        dependencies=[statistics['mode']],
    )
    """number of first returns"""
    first_count = Metric(
        '1st_count',
        np.int32,
        count_fn,
        filters=[first_returns_filter],
        attributes=[A['ReturnNumber']],
    )
    return_counts = {
        f'r{return_number}_count': Metric(
            f'r{return_number}_count',
            np.int32,
            count_fn,
            filters=[make_returns_filter(return_number)],
            attributes=[A['ReturnNumber']],
        )
        for return_number in range(1, 10)
    }
    other_count = Metric(
        'rother_count',
        np.int32,
        count_fn,
        filters=[other_returns_filter],
        attributes=[A['ReturnNumber']],
    )
    counts = {
        all_count.name: all_count,
        all_count_above_htbreak.name: all_count_above_htbreak,
        all_count_above_minht.name: all_count_above_minht,
        all_count_above_mean.name: all_count_above_mean,
        all_count_above_mode.name: all_count_above_mode,
        first_count.name: first_count,
        first_count_above_htbreak.name: first_count_above_htbreak,
        first_count_above_mean.name: first_count_above_mean,
        first_count_above_mode.name: first_count_above_mode,
        **{metric.name: metric for metric in return_counts.values()},
        other_count.name: other_count,
    }
    return counts


counts = get_count_metrics()
