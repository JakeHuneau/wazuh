# Copyright (C) 2015, Wazuh Inc.
# Created by Wazuh, Inc. <info@wazuh.com>.
# This program is a free software; you can redistribute it and/or modify it under the terms of GPLv2

import logging

from aiohttp import web

from api.encoder import dumps, prettify
from api.util import raise_if_exc, remove_nones_to_dict, parse_api_param
from wazuh import engine
from wazuh.core.cluster.dapi.dapi import DistributedAPI
from api.models.base_model_ import Body
from api.models.engine_model import UpdateConfigModel

logger = logging.getLogger('wazuh-api')

# @expose_resources(actions=["engine:read_config"], resources=["engine:config:*"])
async def get_runtime_config(request, pretty: bool = False, wait_for_complete: bool = False,
                             name: str = None, q: str = None, select: str = None, sort: str = None, search: str = None,
                             offset: int = 0, limit: int = None) -> web.Response:
    """Get the runtime configuration of the manager.

    Parameters
    ----------
    request : connexion.request
    pretty : bool
        Show results in human-readable format.
    wait_for_complete : bool
        Disable timeout response.
    name : str
        Name of the configuration option.
    q : str
        Query to filter agents by.
    select : str
        Select which fields to return (separated by comma).
    sort : str
        Sorts the collection by a field or fields (separated by comma). Use +/- at the beginning to list in
        ascending or descending order.
    search : str
        Look for elements with the specified string.
    offset : int
        First element to return in the collection.
    limit : int
        Maximum number of elements to return.

    Returns
    -------
    web.Response
        API response.
    """
    f_kwargs = {
        'name': name,
        'q': q,
        'select': select,
        'sort': parse_api_param(sort, 'sort'),
        'search': parse_api_param(search, 'search'),
        'offset': offset,
        'limit': limit,
        }

    dapi = DistributedAPI(f=engine.get_runtime_config,
                          f_kwargs=remove_nones_to_dict(f_kwargs),
                          request_type='local_master',
                          is_async=False,
                          wait_for_complete=wait_for_complete,
                          logger=logger,
                          rbac_permissions=request['token_info']['rbac_policies']
                          )
    data = raise_if_exc(await dapi.distribute_function())

    return web.json_response(data=data, status=200, dumps=prettify if pretty else dumps)

# @expose_resources(actions=["engine:update_config"], resources=["engine:config:*"])
async def update_runtime_config(request, pretty: bool = False, wait_for_complete: bool = False,
                        save: bool = False) -> web.Response:
    """Update the runtime configuration of the manager.

    Parameters
    ----------
    request : connexion.request
    pretty : bool
        Show results in human-readable format.
    wait_for_complete : bool
        Disable timeout response.
    save : bool
        Save the configuration to disk

    Returns
    -------
    web.Response
        API response.
    """
    Body.validate_content_type(request, expected_content_type='application/json')
    f_kwargs = await UpdateConfigModel.get_kwargs(request, additional_kwargs={'save': save})

    dapi = DistributedAPI(f=engine.update_runtime_config,
                          f_kwargs=remove_nones_to_dict(f_kwargs),
                          request_type='local_master',
                          is_async=False,
                          wait_for_complete=wait_for_complete,
                          logger=logger,
                          rbac_permissions=request['token_info']['rbac_policies']
                          )
    data = raise_if_exc(await dapi.distribute_function())

    return web.json_response(data=data, status=200, dumps=prettify if pretty else dumps)
