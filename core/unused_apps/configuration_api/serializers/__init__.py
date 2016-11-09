from rest_framework import serializers


class UpdateConfigSerializer(serializers.Serializer):
    key = serializers.CharField()
    value = serializers.CharField()


class NewConfigSerializer(serializers.Serializer):
    category = serializers.CharField()
    key = serializers.CharField()
    value = serializers.CharField()


class DeleteConfigSerializer(serializers.Serializer):
    key = serializers.CharField()