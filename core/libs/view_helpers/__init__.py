def format_errors(errors):
    """Format serializer errors to conform to our messaging format. (ie, sending a list of
    messages or a single message under 'success', 'info', 'warning', or 'failure').
    :param errors: An error dictionary as produced by rest_framework serializers.
    :returns: A list of messages."""
    out_errors = []

    for key in errors:
        for msg in errors[key]:
            if key != 'non_field_errors':
                out_errors.append('{}: {}'.format(key, msg))
            else:
                out_errors.append(msg)

    return out_errors

