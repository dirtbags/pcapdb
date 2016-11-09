from django import template
register = template.Library()


@register.filter(name='get_item')
def get_item(dictionary, key):
    """Returns the value at a given variable key"""
    return dictionary.get(key)

