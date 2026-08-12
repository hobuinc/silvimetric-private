from uuid import UUID
from typing_extensions import Union
from typing import Optional
from datetime import datetime
import tiledb

from .. import Storage, Bounds


def info(
    storage: Union[str, Storage],
    dates: Optional[tuple[datetime, datetime]] = None,
    bounds: Optional[Bounds] = None,
    name: Optional[Union[str, UUID]] = None,
    concise: Optional[bool] = False,
) -> dict:
    """
    Collect information about database in current state

    :param storage: SilviMetric Storage or TileDB directory path.
    :param dates: DateTime range query, defaults to None.
    :param bounds: Bounds query, defaults to None.
    :param name: Name query, defaults to None.
    :return: Returns json object containing information on database.
    """
    if (
        isinstance(storage, str)
        and tiledb.object_type(storage, ctx=Storage.get_tdb_context()) == 'group'
    ):
        members = Storage.shard_members(storage)
        storage = members[0]
        group_histories = [
            member.get_history(dates, bounds, name, concise)
            for member in members
        ]
    else:
        group_histories = None
        if isinstance(storage, str):
            storage = Storage.from_db(storage)


    if bounds is None:
        bounds = storage.config.root
    if name is not None:
        if isinstance(name, str):
            name = UUID(name)

    # We always have these
    meta = storage.get_config()
    atts = storage.get_attributes()

    info = {
        'attributes': [a.to_json() for a in atts],
        'metadata': meta.to_json(),
    }

    try:
        history = (
            [item for items in group_histories for item in items]
            if group_histories is not None
            else storage.get_history(dates, bounds, name, concise)
        )
        if bool(history) and isinstance(history, list):
            history = [h for h in history]
        elif bool(history):
            history = [history]
        else:
            history = []

        info['history'] = history
    except KeyError:
        info['history'] = {}
    # remove unnecessary keys for printing to logs
    if concise:
        info['metadata'].pop('attrs')
        info['metadata'].pop('debug')
        info['metadata'].pop('log')

    return info
