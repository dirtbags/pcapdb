from django import template

register = template.Library()

import json


@register.filter(name='pretty_json')
def pretty_json(value):
    """Removes all values of arg from the given string"""
    return json.dumps(value, indent=4)