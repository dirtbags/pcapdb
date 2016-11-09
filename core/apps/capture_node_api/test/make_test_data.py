from django.utils import timezone
import datetime

now = timezone.now()
duration = datetime.timedelta(days=7)
start = now - duration

class Person: 
