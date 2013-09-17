{
  'targets': [
    {
      'target_name': 'sophia',
      'product_prefix': 'lib',
      'type': 'static_library',
      'include_dirs': ['db'],
      'link_settings': {
        'libraries': ['-lpthread'],
      },
      'direct_dependent_settings': {
        'include_dirs': ['db'],
      },
      'sources': [
        'db/cat.c',
        'db/crc.c',
        'db/cursor.c',
        'db/e.c',
        'db/file.c',
        'db/gc.c',
        'db/i.c',
        'db/merge.c',
        'db/recover.c',
        'db/rep.c',
        'db/sp.c',
        'db/util.c',
      ],
    },
  ],
}
