#ifndef CSTATUSVIEW_H
#define CSTATUSVIEW_H

#include "CSimpleFS.h"
#include "CCacheIO.h"

#include<memory>

void ShowStatus(std::weak_ptr<SimpleFilesystem> fs, std::weak_ptr<CCacheIO> cbio);

#endif

