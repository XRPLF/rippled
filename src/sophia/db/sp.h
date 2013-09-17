#ifndef SP_H_
#define SP_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#define _GNU_SOURCE 1

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <errno.h>

#include "sophia.h"

#include "macro.h"
#include "crc.h"
#include "lock.h"
#include "list.h"
#include "e.h"
#include "a.h"
#include "meta.h"
#include "file.h"
#include "ref.h"
#include "i.h"
#include "rep.h"
#include "cat.h"
#include "task.h"
#include "core.h"
#include "util.h"
#include "recover.h"
#include "merge.h"
#include "gc.h"
#include "cursor.h"

#endif
