"""Compile the offline Help library. Requires Python-Markdown only at build time."""
from pathlib import Path
from html import escape
from html.parser import HTMLParser
import json
import re
import sys

import markdown

APP = Path(__file__).resolve().parents[1]
SOURCE = APP / 'help'
OUTPUT = APP / 'renderer' / 'help'
ALLOWED = set('p h1 h2 h3 h4 h5 h6 ul ol li strong em code pre blockquote table thead tbody tr th td hr br img a del details summary'.split())
VOID = {'hr', 'br', 'img'}


def media_url(value, source):
    """All media must exist inside the bundled Help media directory."""
    target = (source.parent / value).resolve()
    root = (OUTPUT / 'media').resolve()
    if not target.is_relative_to(root) or not target.is_file():
        raise ValueError(f'Missing or non-bundled Help media: {source.name}: {value}')
    return 'help/media/' + target.relative_to(root).as_posix()


class SafeDocument(HTMLParser):
    def __init__(self, source, ids, sources):
        super().__init__(convert_charrefs=True)
        self.source, self.ids, self.sources, self.parts, self.skip = source, ids, sources, [], 0

    def handle_starttag(self, tag, attrs):
        if tag in {'script', 'style', 'iframe', 'object'}:
            self.skip += 1
            return
        if self.skip or tag not in ALLOWED:
            return
        attrs = dict(attrs)
        clean = {}
        if tag.startswith('h') and tag[1:].isdigit() and attrs.get('id'):
            clean['id'] = 'help-section-' + attrs['id']
        if tag == 'a':
            href = attrs.get('href', '')
            if href.startswith('help:'):
                doc = href[5:].split('#', 1)[0]
                if doc not in self.ids:
                    raise ValueError(f'Unknown Help article: {href} in {self.source}')
                clean = {'href': '#', 'data-help-link': href[5:]}
            elif href.startswith('#'):
                clean = {'href': '#', 'data-help-section': href[1:]}
            elif href.startswith('https://'):
                clean = {'href': '#', 'data-help-web': href, 'title': 'Open in your browser'}
            elif href:
                path, _, fragment = href.partition('#')
                doc = self.sources.get((self.source.parent / path).resolve())
                if doc:
                    clean = {'href': '#', 'data-help-link': doc + ('#' + fragment if fragment else '')}
            # Non-bundled file and source references remain readable text.
        if tag == 'img':
            clean = {'src': media_url(attrs.get('src', ''), self.source), 'alt': attrs.get('alt', ''), 'loading': 'lazy', 'decoding': 'async', 'tabindex': '0', 'role': 'button', 'title': 'Enlarge picture'}
        if tag in {'th', 'td'} and attrs.get('align') in {'left', 'right', 'center'}:
            clean['class'] = 'help-align-' + attrs['align']
        self.parts.append('<' + tag + ''.join(f' {k}="{escape(v, quote=True)}"' for k, v in clean.items()) + '>')

    def handle_endtag(self, tag):
        if tag in {'script', 'style', 'iframe', 'object'}:
            self.skip = max(0, self.skip - 1)
        elif not self.skip and tag in ALLOWED and tag not in VOID:
            self.parts.append('</' + tag + '>')

    def handle_data(self, data):
        if not self.skip:
            self.parts.append(escape(data))


def build():
    entries = json.loads((SOURCE / 'catalogue.json').read_text(encoding='utf-8'))
    ids = {e['id'] for e in entries}
    if len(ids) != len(entries):
        raise ValueError('Duplicate Help article ID')
    paths = {e['id']: (SOURCE / e['source']).resolve() if e.get('source') else SOURCE / 'docs' / (e['id'] + '.md') for e in entries}
    sources = {p.resolve(): id for id, p in paths.items()}
    docs = []
    for entry in entries:
        source = paths[entry['id']]
        text = source.read_text(encoding='utf-8')
        md = markdown.Markdown(extensions=['tables', 'fenced_code', 'toc', 'sane_lists'])
        rendered = md.convert(text)
        # Title is shown by the reader's header, once.
        rendered = re.sub(r'^<h1\b[^>]*>.*?</h1>\s*', '', rendered, count=1, flags=re.S)
        safe = SafeDocument(source, ids, sources)
        safe.feed(rendered)
        def headings(tokens):
            result = []
            for token in tokens:
                if token['level'] > 1:
                    result.append({'id': token['id'], 'title': re.sub('<[^>]+>', '', token['name']), 'level': token['level']})
                result.extend(headings(token.get('children', [])))
            return result
        doc = {**entry, 'html': ''.join(safe.parts), 'toc': headings(md.toc_tokens), 'text': re.sub(r'!\[[^]]*\]\([^)]*\)', '', text)}
        doc['videos'] = [{**v, 'src': media_url(v['file'], source)} for v in entry.get('videos', [])]
        poster = re.search(r'<img src="([^"]+)"', doc['html'])
        if poster:
            for video in doc['videos']:
                video['poster'] = poster.group(1)
        docs.append(doc)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / 'library.js').write_text('// Built by tools/build-help.py; edit help/docs and help/catalogue.json.\nconst LAB_HELP_LIBRARY = ' + json.dumps(docs, ensure_ascii=False, separators=(',', ':')) + ';\n', encoding='utf-8')
    print(f'Built {len(docs)} offline Help articles, {sum(len(d["videos"]) for d in docs)} videos.')


if __name__ == '__main__':
    try:
        build()
    except (ValueError, FileNotFoundError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
