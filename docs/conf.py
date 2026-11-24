# -- Import setup -------------------------------------------------------------

from os import path
import os

# -- Project information ------------------------------------------------------

repository = 'kuiper-imager'
project = 'Kuiper Imager'
copyright = '2025, Analog Devices, Inc.'
author = 'Analog Devices, Inc.'

# Version from environment variable or fallback
version = os.environ.get('ADOC_DOC_VERSION', 'latest')

# -- General configuration ----------------------------------------------------

extensions = [
    'sphinx.ext.todo',
    'adi_doctools',
]

needs_extensions = {
    'adi_doctools': '0.3.47'
}

exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']
source_suffix = '.rst'

# -- Numbered references configuration ----------------------------------------

numfig = True
numfig_format = {
    'figure': 'Figure %s',
    'table': 'Table %s',
    'code-block': 'Listing %s',
    'section': 'Section %s'
}

# -- External docs configuration ----------------------------------------------

interref_repos = []

# -- Custom extensions configuration ------------------------------------------

hide_collapsible_content = True
validate_links = False

# -- todo configuration -------------------------------------------------------

# The pages are fully written, so no `.. todo::` should remain. Emitting warnings
# for any stray todo makes the strict `-W` CI build fail, keeping the docs honest.
todo_include_todos = True
todo_emit_warnings = True

# -- Options for HTML output --------------------------------------------------

html_theme = 'cosmic'
html_static_path = ['resources']
html_favicon = path.join("resources", "icon.png")

html_theme_options = {
    "light_logo": "logo_light.png",
    "dark_logo": "logo_dark.png",
}
