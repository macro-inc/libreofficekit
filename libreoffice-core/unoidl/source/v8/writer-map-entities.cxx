#include <iostream>
#include <map>

#include <unoidl/unoidl.hxx>
#include <rtl/byteseq.hxx>
#include "writer.hxx"

namespace writer {

void markNamespaceRelevant(OUString const& name, std::map<OUString, writer::Entity*>& map,
                           rtl::Reference<unoidl::Manager> const& manager, bool relevant) {
    if (!relevant)
        return;

    std::map<OUString, writer::Entity*>* map2 = &map;
    std::set<OUString>* deps;
    for (sal_Int32 j = 0;;) {
        OUString id(name.getToken(0, '.', j));
        if (j == -1) {
            deps->emplace(id);
            break;
        }

        // propogate relevant
        std::map<OUString, writer::Entity*>::iterator k(map2->find(id));
        if (k == map2->end()) {
            rtl::Reference<unoidl::Entity> ent2(manager->findEntity(name.copy(0, j - 1)));
            assert(ent2.is());
            auto* entity = new writer::Entity(ent2, false);
            k = map2->insert(std::make_pair(id, entity)).first;
        }
        assert(k->second.entity->getSort() == unoidl::Entity::SORT_MODULE);
        k->second->relevant = true;
        deps = &k->second->dependencies;
        map2 = &k->second->module;
    }
}

void insertEntityDependency(rtl::Reference<unoidl::Manager> const& manager,
                            std::map<OUString, writer::Entity*>::iterator const& iterator,
                            std::map<OUString, writer::Entity*>& map, OUString const& name,
                            bool weakInterfaceDependency = false) {
    assert(manager.is());
    if (name == iterator->first)
        return;

    bool ifc = false;
    if (weakInterfaceDependency) {
        rtl::Reference<unoidl::Entity> ent(manager->findEntity(name));
        if (!ent.is()) {
            std::cerr << "Unknown entity " << name << std::endl;
            std::exit(EXIT_FAILURE);
        }
        ifc = ent->getSort() == unoidl::Entity::SORT_INTERFACE_TYPE;
        if (ent->getSort() == unoidl::Entity::SORT_EXCEPTION_TYPE)
            return;
    }
    markNamespaceRelevant(name, map, manager, true);
    (ifc ? iterator->second->interfaceDependencies : iterator->second->dependencies).insert(name);
}

void insertEntityDependencies(rtl::Reference<unoidl::Manager> const& manager,
                              std::map<OUString, writer::Entity*>::iterator const& iterator,
                              std::map<OUString, writer::Entity*>& map,
                              std::vector<OUString> const& names) {
    for (auto& i : names) {
        insertEntityDependency(manager, iterator, map, i);
    }
}

void insertEntityDependencies(rtl::Reference<unoidl::Manager> const& manager,
                              std::map<OUString, writer::Entity*>::iterator const& iterator,
                              std::map<OUString, writer::Entity*>& map,
                              std::vector<unoidl::AnnotatedReference> const& references) {
    for (auto& i : references) {
        insertEntityDependency(manager, iterator, map, i.name);
    }
}

void insertTypeDependency(rtl::Reference<unoidl::Manager> const& manager,
                          std::map<OUString, writer::Entity*>::iterator const& iterator,
                          std::map<OUString, writer::Entity*>& map, OUString const& type) {
    std::size_t rank;
    std::vector<OUString> args;
    bool entity;
    OUString nucl(writer::decomposeType(type, &rank, &args, &entity));
    if (entity) {
        /** see special case in ::writeType */
        if (rank > 0 && nucl == "com.sun.star.beans.PropertyValue") {
            iterator->second->dependencies.insert("com.sun.star.uno.Any");
            return;
        }

        insertEntityDependency(manager, iterator, map, nucl, true);
        for (const auto& i : args) {
            insertTypeDependency(manager, iterator, map, i);
        }
    }
    if (rank > 0) {
        iterator->second->dependencies.insert("com.sun.star.uno.Sequence");
    } else if (nucl == "type") {
        iterator->second->dependencies.insert("com.sun.star.uno.Type");
    } else if (nucl == "any") {
        iterator->second->dependencies.insert("com.sun.star.uno.Any");
    }
}

void insertDependency(rtl::Reference<unoidl::Entity> ent,
                      rtl::Reference<unoidl::Manager> const& manager,
                      std::map<OUString, writer::Entity*>& map,
                      std::map<OUString, Entity*>::iterator i) {
    switch (ent->getSort()) {
        case unoidl::Entity::SORT_ENUM_TYPE:
        case unoidl::Entity::SORT_CONSTANT_GROUP:
            break;
        case unoidl::Entity::SORT_PLAIN_STRUCT_TYPE: {
            rtl::Reference<unoidl::PlainStructTypeEntity> ent2(
                static_cast<unoidl::PlainStructTypeEntity*>(ent.get()));
            if (!ent2->getDirectBase().isEmpty()) {
                insertEntityDependency(manager, i, map, ent2->getDirectBase());
            }
            for (auto& j : ent2->getDirectMembers()) {
                insertTypeDependency(manager, i, map, j.type);
            }
            break;
        }
        case unoidl::Entity::SORT_POLYMORPHIC_STRUCT_TYPE_TEMPLATE: {
            rtl::Reference<unoidl::PolymorphicStructTypeTemplateEntity> ent2(
                static_cast<unoidl::PolymorphicStructTypeTemplateEntity*>(ent.get()));
            for (auto& j : ent2->getMembers()) {
                if (!j.parameterized) {
                    insertTypeDependency(manager, i, map, j.type);
                }
            }
            break;
        }
        case unoidl::Entity::SORT_EXCEPTION_TYPE: {
            rtl::Reference<unoidl::ExceptionTypeEntity> ent2(
                static_cast<unoidl::ExceptionTypeEntity*>(ent.get()));
            if (!ent2->getDirectBase().isEmpty()) {
                insertEntityDependency(manager, i, map, ent2->getDirectBase());
            }
            for (auto& j : ent2->getDirectMembers()) {
                insertTypeDependency(manager, i, map, j.type);
            }
            break;
        }
        case unoidl::Entity::SORT_INTERFACE_TYPE: {
            rtl::Reference<unoidl::InterfaceTypeEntity> ent2(
                static_cast<unoidl::InterfaceTypeEntity*>(ent.get()));
            insertEntityDependencies(manager, i, map, ent2->getDirectMandatoryBases());
            insertEntityDependencies(manager, i, map, ent2->getDirectOptionalBases());
            for (auto& j : ent2->getDirectAttributes()) {
                insertTypeDependency(manager, i, map, j.type);
            }
            for (auto& j : ent2->getDirectMethods()) {
                insertTypeDependency(manager, i, map, j.returnType);
                for (auto& k : j.parameters) {
                    insertTypeDependency(manager, i, map, k.type);
                }
                insertEntityDependencies(manager, i, map, j.exceptions);
            }
            break;
        }
        case unoidl::Entity::SORT_TYPEDEF: {
            rtl::Reference<unoidl::TypedefEntity> ent2(
                static_cast<unoidl::TypedefEntity*>(ent.get()));
            insertTypeDependency(manager, i, map, ent2->getType());
            break;
        }
        case unoidl::Entity::SORT_SINGLE_INTERFACE_BASED_SERVICE: {
            rtl::Reference<unoidl::SingleInterfaceBasedServiceEntity> ent2(
                static_cast<unoidl::SingleInterfaceBasedServiceEntity*>(ent.get()));
            insertEntityDependency(manager, i, map, ent2->getBase());
            for (auto& j : ent2->getConstructors()) {
                for (auto& k : j.parameters) {
                    insertTypeDependency(manager, i, map, k.type);
                }
                insertEntityDependencies(manager, i, map, j.exceptions);
            }
            break;
        }
        case unoidl::Entity::SORT_ACCUMULATION_BASED_SERVICE: {
            rtl::Reference<unoidl::AccumulationBasedServiceEntity> ent2(
                static_cast<unoidl::AccumulationBasedServiceEntity*>(ent.get()));
            insertEntityDependencies(manager, i, map, ent2->getDirectMandatoryBaseServices());
            insertEntityDependencies(manager, i, map, ent2->getDirectOptionalBaseServices());
            insertEntityDependencies(manager, i, map, ent2->getDirectMandatoryBaseInterfaces());
            insertEntityDependencies(manager, i, map, ent2->getDirectOptionalBaseInterfaces());
            for (auto& j : ent2->getDirectProperties()) {
                insertTypeDependency(manager, i, map, j.type);
            }
            break;
        }
        case unoidl::Entity::SORT_INTERFACE_BASED_SINGLETON: {
            rtl::Reference<unoidl::InterfaceBasedSingletonEntity> ent2(
                static_cast<unoidl::InterfaceBasedSingletonEntity*>(ent.get()));
            insertEntityDependency(manager, i, map, ent2->getBase());
            break;
        }
        case unoidl::Entity::SORT_SERVICE_BASED_SINGLETON: {
            rtl::Reference<unoidl::ServiceBasedSingletonEntity> ent2(
                static_cast<unoidl::ServiceBasedSingletonEntity*>(ent.get()));
            insertEntityDependency(manager, i, map, ent2->getBase());
            break;
        }
        case unoidl::Entity::SORT_MODULE:
            assert(false && "this cannot happen");
    }
}

void mapEntities(rtl::Reference<unoidl::Manager> const& manager, OUString const& uri,
                 std::map<OUString, writer::Entity*>& map,
                 std::map<OUString, writer::Entity*>& flatMap) {
    assert(manager.is());
    osl::File f(uri);
    osl::FileBase::RC e = f.open(osl_File_OpenFlag_Read);
    if (e != osl::FileBase::E_None) {
        std::cerr << "Cannot open <" << f.getURL() << "> for reading, error code " << +e
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }
    for (;;) {
        sal_Bool eof;
        e = f.isEndOfFile(&eof);
        if (e != osl::FileBase::E_None) {
            std::cerr << "Cannot check <" << f.getURL() << "> for EOF, error code " << +e
                      << std::endl;
            std::exit(EXIT_FAILURE);
        }
        if (eof) {
            break;
        }
        rtl::ByteSequence s1;
        e = f.readLine(s1);
        if (e != osl::FileBase::E_None) {
            std::cerr << "Cannot read from <" << f.getURL() << ">, error code " << +e << std::endl;
            std::exit(EXIT_FAILURE);
        }
        OUString s2;
        if (!rtl_convertStringToUString(
                &s2.pData, reinterpret_cast<char const*>(s1.getConstArray()), s1.getLength(),
                RTL_TEXTENCODING_UTF8,
                (RTL_TEXTTOUNICODE_FLAGS_UNDEFINED_ERROR | RTL_TEXTTOUNICODE_FLAGS_MBUNDEFINED_ERROR
                 | RTL_TEXTTOUNICODE_FLAGS_INVALID_ERROR))) {
            std::cerr << "Cannot interpret line read from <" << f.getURL() << "> as UTF-8"
                      << std::endl;
            std::exit(EXIT_FAILURE);
        }
        for (sal_Int32 i = 0; i != -1;) {
            OUString entityWithNamespace(s2.getToken(0, ' ', i));
            if (!entityWithNamespace.isEmpty()) {
                rtl::Reference<unoidl::Entity> ent(manager->findEntity(entityWithNamespace));
                if (!ent.is()) {
                    std::cerr << "Unknown entity \"" << entityWithNamespace << "\" read from <"
                              << f.getURL() << ">" << std::endl;
                    std::exit(EXIT_FAILURE);
                }
                if (ent->getSort() == unoidl::Entity::SORT_MODULE) {
                    std::cerr << "Module entity \"" << entityWithNamespace << "\" read from <"
                              << f.getURL() << ">" << std::endl;
                    std::exit(EXIT_FAILURE);
                }
                std::map<OUString, writer::Entity*>* map2 = &map;
                for (sal_Int32 j = 0;;) {
                    OUString id(entityWithNamespace.getToken(0, '.', j));
                    if (j == -1) {
                        auto relevant
                            = ent->getSort() != unoidl::Entity::SORT_MODULE
                              && ent->getSort() != unoidl::Entity::SORT_EXCEPTION_TYPE
                              && static_cast<unoidl::PublishableEntity*>(ent.get())->isPublished();
                        auto* entity = new writer::Entity(ent, relevant);

                        // insert into the flat map
                        std::map<OUString, writer::Entity*>::iterator k2(
                            flatMap.insert(std::make_pair(entityWithNamespace, entity)).first);
                        insertDependency(ent, manager, map, k2);
                        markNamespaceRelevant(entityWithNamespace, map, manager, relevant);

                        break;
                    }

                    // build the module hierarchy
                    std::map<OUString, writer::Entity*>::iterator k(map2->find(id));
                    if (k == map2->end()) {
                        rtl::Reference<unoidl::Entity> ent2(
                            manager->findEntity(entityWithNamespace.copy(0, j - 1)));
                        assert(ent2.is());
                        auto* entity = new writer::Entity(ent2, false);
                        k = map2->insert(std::make_pair(id, entity)).first;
                    }
                    assert(k->second.entity->getSort() == unoidl::Entity::SORT_MODULE);
                    map2 = &k->second->module;
                }
            }
        }
    }
    e = f.close();
    if (e != osl::FileBase::E_None) {
        std::cerr << "Cannot close <" << f.getURL() << "> after reading, error code " << +e
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }
}
}
