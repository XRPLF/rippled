import os

def migrate(home_folder):
    from conans.client.graph.compatibility import migrate_compatibility_files
    migrate_compatibility_files(home_folder)
