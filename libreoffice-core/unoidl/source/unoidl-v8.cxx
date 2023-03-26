/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <config_version.h>
#include <osl/endian.h>
#include <osl/file.h>
#include <osl/process.h>
#include <rtl/process.h>
#include <rtl/string.h>
#include <rtl/textcvt.h>
#include <rtl/textenc.h>
#include <sal/config.h>
#include <sal/macros.h>
#include <sal/main.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <osl/file.hxx>
#include <rtl/byteseq.hxx>
#include <rtl/string.hxx>
#include <rtl/ustring.hxx>
#include <set>
#include <unoidl/unoidl.hxx>
#include <utility>
#include <vector>

#include "v8/writer.hxx"

namespace {

void badUsage() {
    std::cerr << "Usage:" << std::endl
              << std::endl
              << "  unoidl-v8 [internal] [<registries>] [@<entities file>] <out dir>" << std::endl
              << std::endl
              << "where each <registry> is either a new- or legacy-format .rdb file, a single .idl"
              << std::endl
              << "file, or a root directory of an .idl file tree; and the UTF-8 encoded <entities"
              << std::endl
              << "file> contains zero or more space-separated names of (non-module) entities to"
              << std::endl
              << "include in the output, and, if omitted, defaults to the complete content of the"
              << std::endl
              << "last <registry>, if any. The generated files will be placed in <out dir>. If"
              << std::endl
              << "the internal option is set, the functions used by the generated bindings will be"
              << std::endl
              << "produced instead.";
    std::exit(EXIT_FAILURE);
}

OUString getArgumentUri(sal_uInt32 argument, bool* entities) {
    OUString arg;
    rtl_getAppCommandArg(argument, &arg.pData);
    if (arg.equals("internal"))
        return arg;
    if (arg.startsWith("@", &arg)) {
        if (entities == nullptr) {
            badUsage();
        }
        *entities = true;
    } else if (entities != nullptr) {
        *entities = false;
    }
    OUString url;
    osl::FileBase::RC e1 = osl::FileBase::getFileURLFromSystemPath(arg, url);
    if (e1 != osl::FileBase::E_None) {
        std::cerr << "Cannot convert \"" << arg << "\" to file URL, error code " << +e1
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }
    OUString cwd;
    oslProcessError e2 = osl_getProcessWorkingDir(&cwd.pData);
    if (e2 != osl_Process_E_None) {
        std::cerr << "Cannot obtain working directory, error code " << +e2 << std::endl;
        std::exit(EXIT_FAILURE);
    }
    OUString abs;
    e1 = osl::FileBase::getAbsoluteFileURL(cwd, url, abs);
    if (e1 != osl::FileBase::E_None) {
        std::cerr << "Cannot make \"" << url << "\" into an absolute file URL, error code " << +e1
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }
    return abs;
}

void mapCursor(rtl::Reference<unoidl::MapCursor> const& cursor,
               std::map<OUString, writer::Entity*>& map) {
    if (!cursor.is())
        return;

    for (;;) {
        OUString name;
        rtl::Reference<unoidl::Entity> ent(cursor->getNext(&name));
        if (!ent.is()) {
            break;
        }
        auto relevant = ent->getSort() != unoidl::Entity::SORT_MODULE
                        && static_cast<unoidl::PublishableEntity*>(ent.get())->isPublished();
        auto* entity = new writer::Entity(ent, relevant);
        std::pair<std::map<OUString, writer::Entity*>::iterator, bool> i(
            map.insert(std::make_pair(name, entity)));
        if (!i.second) {
            std::cout << "Duplicate name \"" << name << '"' << std::endl;
            std::exit(EXIT_FAILURE);
        }
        if (i.first->second->entity->getSort() == unoidl::Entity::SORT_MODULE) {
            mapCursor(rtl::Reference<unoidl::ModuleEntity>(
                          static_cast<unoidl::ModuleEntity*>(i.first->second->entity.get()))
                          ->createCursor(),
                      i.first->second->module);
        }
    }
}

void propagateRelevant(std::map<OUString, writer::Entity*>& entities, writer::Entity& entity) {
    if (!entity.relevant) {
        entity.relevant = true;
        if (entity.sorted != writer::Entity::Sorted::YES) {
            for (auto& i : entity.dependencies) {
                std::map<OUString, writer::Entity*>::iterator j(entities.find(i));
                if (j != entities.end()) {
                    propagateRelevant(entities, *j->second);
                }
            }
        }
    }
}

void markRelevantStruct(std::map<OUString, writer::Entity*>& entities,
                        unoidl::PlainStructTypeEntity* entity) {
    for (auto& k : entity->getDirectMembers()) {
        std::map<OUString, writer::Entity*>::iterator j(entities.find(k.type));
        if (j != entities.end()) {
            j->second->relevant = true;
        }
    }

    if (!entity->getDirectBase().isEmpty()) {
        std::map<OUString, writer::Entity*>::iterator j(entities.find(entity->getDirectBase()));
        if (j != entities.end()) {
            j->second->relevant = true;
            markRelevantStruct(
                entities, static_cast<unoidl::PlainStructTypeEntity*>(j->second->entity.get()));
        }
    }
}

void visit(std::map<OUString, writer::Entity*>& entities,
           std::map<OUString, writer::Entity*>::iterator const& iterator,
           std::vector<OUString>& result) {
    // polystructs are unsupported and this is pretty much the only use of it that's marked relevant
    if (iterator->first.endsWith("awt.ItemListEvent")) {
        iterator->second->relevant = false;
        return;
    }

    switch (iterator->second->sorted) {
        case writer::Entity::Sorted::NO:
            iterator->second->sorted = writer::Entity::Sorted::ACTIVE;
            for (auto& i : iterator->second->dependencies) {
                std::map<OUString, writer::Entity*>::iterator j(entities.find(i));
                if (j != entities.end()) {
                    if (iterator->second->relevant) {
                        propagateRelevant(entities, *j->second);
                    }
                    visit(entities, j, result);
                }
            }

            // fix missing dependencies on method types
            if (iterator->second->entity->getSort() == unoidl::Entity::SORT_INTERFACE_TYPE) {
                auto* intf
                    = static_cast<unoidl::InterfaceTypeEntity*>(iterator->second->entity.get());

                for (auto& k : intf->getDirectMethods()) {
                    if (writer::shouldSkipMethod(k))
                        continue;

                    std::map<OUString, writer::Entity*>::iterator j(entities.find(k.returnType));
                    if (j != entities.end()) {
                        j->second->relevant = true;
                    }

                    for (auto& param : k.parameters) {
                        j = entities.find(param.type);
                        if (j != entities.end()) {
                            j->second->relevant = true;
                        }
                    }
                }
            }

            if (iterator->second->entity->getSort() == unoidl::Entity::SORT_PLAIN_STRUCT_TYPE) {
                markRelevantStruct(entities, static_cast<unoidl::PlainStructTypeEntity*>(
                                                 iterator->second->entity.get()));
            }
            iterator->second->sorted = writer::Entity::Sorted::YES;
            result.push_back(iterator->first);
            break;
        case writer::Entity::Sorted::ACTIVE:
            std::cerr << "Entity " << iterator->first << " recursively depends on itself"
                      << std::endl;
            std::exit(EXIT_FAILURE);
            // fall-through avoids warnings
        default:
            break;
    }
}

std::vector<OUString> sort(std::map<OUString, writer::Entity*>& entities) {
    std::vector<OUString> res;
    for (auto i(entities.begin()); i != entities.end(); ++i) {
        visit(entities, i, res);
    }
    return res;
}

void writeTSModuleMap(std::map<OUString, writer::Entity*>& entities,
                      writer::TypeScriptWriter* writer, OUString const& prefix = "") {
    for (auto& i : entities) {
        if (i.second->entity->getSort() != unoidl::Entity::SORT_MODULE) {
            continue;
        }

        writeTSModuleMap(i.second->module, writer, prefix + i.first + ".");

        if (i.first == "com" || (i.first == "sun" && prefix == "com.")) {
            continue;
        }

        writer->writeTSIndex(prefix + i.first, i.second);
    }
}

} // namespace

SAL_IMPLEMENT_MAIN() {
    try {
        sal_uInt32 args = rtl_getAppCommandArgCount();
        if (args == 0) {
            badUsage();
        }
        rtl::Reference<unoidl::Manager> mgr(new unoidl::Manager);
        bool entities = false;
        bool internal = false;
        rtl::Reference<unoidl::Provider> prov;
        std::map<OUString, writer::Entity*> nestedMap;
        std::map<OUString, writer::Entity*> flatMap;

        for (sal_uInt32 i = 0; i != args - 1; ++i) {
            assert(args > 1);
            OUString arg = getArgumentUri(i, i == args - 2 ? &entities : nullptr);
            if (i == 0 && arg.equals("internal")) {
                internal = true;
                continue;
            }
            OUString uri(arg);
            if (entities) {
                std::cerr << "ENTITIES MAPPED: " << uri << std::endl;
                mapEntities(mgr, uri, nestedMap, flatMap);
            } else {
                std::cerr << "PROVIDER: " << uri << std::endl;
                try {
                    prov = mgr->addProvider(uri);
                } catch (unoidl::NoSuchFileException&) {
                    std::cerr << "Input <" << uri << "> does not exist" << std::endl;
                    std::exit(EXIT_FAILURE);
                }
            }
        }
        if (!entities) {
            mapCursor((prov.is() ? prov->createRootCursor() : rtl::Reference<unoidl::MapCursor>()),
                      nestedMap);
        }

        std::vector<OUString> sorted(sort(flatMap));
        std::cerr << "Writing " << sorted.size() << " published entities" << std::endl;

        if (internal) {
            std::cerr << "Writing internal bindings..." << std::endl;
            auto* w
                = new writer::V8WriterInternal(flatMap, getArgumentUri(args - 1, nullptr), sorted);
            w->writeInternalHeader();
            w->writeSharedHeader();
            return EXIT_SUCCESS;
        }

        {
            auto* w = new writer::TypeScriptWriter(flatMap, getArgumentUri(args - 1, nullptr)
                                                                + "/typescript");

            for (const auto& i : sorted) {
                std::map<OUString, writer::Entity*>::iterator j(flatMap.find(i));
                // skip irrelevant entities and those without bindings in JS/C++
                if (j == flatMap.end() || !j->second->relevant
                    || j->second->entity->getSort() == unoidl::Entity::SORT_EXCEPTION_TYPE)
                    continue;

                w->createEntityFile(i, ".d.ts");
                w->writeEntity(i);
                w->close();
            }

            writeTSModuleMap(nestedMap, w);
        }

        {
            auto* w = new writer::V8Writer(flatMap, getArgumentUri(args - 1, nullptr) + "/include",
                                           sorted);
            w->createEntityFile("unov8", ".hxx");

            w->writeHeaderIncludes();
            w->writeDeclarations();
            w->writeSimpleTypeConverter();
            w->writeAnyTypeConverter();

            for (const auto& i : sorted) {
                std::map<OUString, writer::Entity*>::iterator j(flatMap.find(i));
                // skip irrelevant entities and those without bindings in JS/C++
                if (j == flatMap.end() || !j->second->relevant
                    || j->second->entity->getSort() == unoidl::Entity::SORT_EXCEPTION_TYPE)
                    continue;

                // not really writing, just sorting
                w->writeEntity(i);
            }

            w->writeOrganizedEntities();

            w->close();
        }

        {
            auto* w
                = new writer::V8Writer(flatMap, getArgumentUri(args - 1, nullptr) + "/v8", sorted);
            w->writeAsUtility();
            w->writeBuildFile();
        }

        return EXIT_SUCCESS;
    } catch (unoidl::FileFormatException& e1) {
        std::cerr << "Bad input <" << e1.getUri() << ">: " << e1.getDetail() << std::endl;
        std::exit(EXIT_FAILURE);
    } catch (std::exception& e1) {
        std::cerr << "Failure: " << e1.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
