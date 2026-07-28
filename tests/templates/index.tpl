<title>{{ title }}</title>
<p>Hello</p>
<ul>
{% for item in items %}
<li>{{ item }}</li>
{% endfor %}
</ul>
{% include "footer.tpl" %}
<p>missing={{ nope }}</p>
