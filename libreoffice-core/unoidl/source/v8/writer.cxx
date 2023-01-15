#include <iostream>
#include <sal/types.h>

#include "writer.hxx"

namespace writer {

void BaseWriter::writeDoc(OUString const& doc) {
    if (!doc.isEmpty())
        out("/** " + doc + " */");
}

void BaseWriter::writeDoc(rtl::Reference<unoidl::Entity> const& entity) {
    assert(entity.is());
    writeDoc(entity->getDoc());
}

void BaseWriter::writeEntity(OUString const& name) {
    std::map<OUString, Entity*>::iterator i(entities_.find(name));
    if (i == entities_.end() || !i->second->relevant)
        return;

    assert(i->second.written != Entity::Written::DEFINITION);
    i->second->written = Entity::Written::DEFINITION;

    createEntityFile(name);

    for (auto& j : i->second->interfaceDependencies) {
        std::map<OUString, Entity*>::iterator k(entities_.find(j));
        if (k != entities_.end() && k->second->written == Entity::Written::NO) {
            k->second->written = Entity::Written::DECLARATION;

            if (k->second->entity->getSort() != unoidl::Entity::SORT_INTERFACE_TYPE) {
                std::cerr << "Entity " << j << " should be an interface type" << std::endl;
                std::exit(EXIT_FAILURE);
            }

            writeInterfaceDependency(
                name, j,
                static_cast<unoidl::PublishableEntity*>(k->second->entity.get())->isPublished());
        }
    }

    rtl::Reference<unoidl::PublishableEntity> ent(
        static_cast<unoidl::PublishableEntity*>(i->second->entity.get()));
    writeDoc(ent);

    switch (ent->getSort()) {
        case unoidl::Entity::SORT_ENUM_TYPE: {
            writeEnum(name, static_cast<unoidl::EnumTypeEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_PLAIN_STRUCT_TYPE: {
            writePlainStruct(name, static_cast<unoidl::PlainStructTypeEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_POLYMORPHIC_STRUCT_TYPE_TEMPLATE: {
            writePolymorphicStruct(
                name, static_cast<unoidl::PolymorphicStructTypeTemplateEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_EXCEPTION_TYPE: {
            writeException(name, static_cast<unoidl::ExceptionTypeEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_INTERFACE_TYPE: {
            writeInterface(name, static_cast<unoidl::InterfaceTypeEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_TYPEDEF: {
            writeTypedef(name, static_cast<unoidl::TypedefEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_CONSTANT_GROUP: {
            writeConstantGroup(name, static_cast<unoidl::ConstantGroupEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_SINGLE_INTERFACE_BASED_SERVICE: {
            writeSingleInterfaceService(
                name, static_cast<unoidl::SingleInterfaceBasedServiceEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_ACCUMULATION_BASED_SERVICE: {
            writeAccumulationService(
                name, static_cast<unoidl::AccumulationBasedServiceEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_INTERFACE_BASED_SINGLETON: {
            writeInterfaceSingleton(name,
                                    static_cast<unoidl::InterfaceBasedSingletonEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_SERVICE_BASED_SINGLETON: {
            writeServiceSingleton(name,
                                  static_cast<unoidl::ServiceBasedSingletonEntity*>(ent.get()));
            break;
        }
        case unoidl::Entity::SORT_MODULE:
            assert(false && "this cannot happen");
    }
}

void BaseWriter::out(OUString const& text) {
    assert(file != nullptr);

    sal_uInt64 n;
    OString utf8 = text.toUtf8();
    sal_uInt64 size = utf8.getLength();
    osl::FileBase::RC e = file_->write(utf8.getStr(), size, n);
    if (e != osl::FileBase::E_None) {
        std::cerr << "Cannot write to <" << file_->getURL() << ">, error code " << +e << std::endl;
        std::exit(EXIT_FAILURE);
    }
    if (n != size) {
        std::cerr << "Bad write of " << n << " instead of " << size << " bytes to <"
                  << file_->getURL() << '>' << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

OUString simplifyNamespace(OUString const& name) {
    return name.replaceFirst("com.sun.star.", "api.");
}

void BaseWriter::createEntityFile(OUString const& entityName) {
    if (file_)
        file_->close();

    OUString fileUrl(outDirectoryUrl_ + "/" + simplifyNamespace(entityName).replaceAll(".", "/"));

    OUString dir(fileUrl.subView(0, fileUrl.lastIndexOf('/')));

    switch (osl::Directory::createPath(dir)) {
        case osl::FileBase::E_None:
        case osl::FileBase::E_EXIST:
            break;
        default:
            std::cerr << "Unable to open " << file_->getURL() << std::endl;
            std::exit(EXIT_FAILURE);
            break;
    }

    file_ = new osl::File(fileUrl);
    osl::File::RC err = file_->open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);

    // truncate existing file
    if (err == osl::File::E_EXIST) {
        err = file_->open(osl_File_OpenFlag_Write);
        if (err == osl::File::E_None)
            err = file_->setSize(0);
    }
    if (err != osl::File::E_None) {
        std::cerr << "Unable to open " << file_->getURL() << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

OUString decomposeType(OUString const& type, std::size_t* rank,
                       std::vector<OUString>* typeArguments, bool* entity) {
    assert(rank != nullptr);
    assert(typeArguments != nullptr);
    assert(entity != nullptr);
    OUString nucl(type);
    *rank = 0;
    typeArguments->clear();

    // pop the [] off the beginning of the type
    while (nucl.startsWith("[]", &nucl)) {
        ++*rank;
    }

    // construct the type arguments
    sal_Int32 i = nucl.indexOf('<');
    if (i != -1) {
        OUString tmpl(nucl.copy(0, i));
        do {
            ++i; // skip '<' or ','
            sal_Int32 j = i;
            for (sal_Int32 level = 0; j != nucl.getLength(); ++j) {
                sal_Unicode c = nucl[j];
                if (c == ',') {
                    if (level == 0) {
                        break;
                    }
                } else if (c == '<') {
                    ++level;
                } else if (c == '>') {
                    if (level == 0) {
                        break;
                    }
                    --level;
                }
            }
            if (j != nucl.getLength()) {
                typeArguments->push_back(nucl.copy(i, j - i));
            }
            i = j;
        } while (i != nucl.getLength() && nucl[i] != '>');
        assert(i == nucl.getLength() - 1 && nucl[i] == '>');
        assert(!typeArguments->empty());
        nucl = tmpl;
    }

    assert(!nucl.isEmpty());
    *entity = nucl != "void" && nucl != "boolean" && nucl != "byte" && nucl != "short"
              && nucl != "unsigned short" && nucl != "long" && nucl != "unsigned long"
              && nucl != "hyper" && nucl != "unsigned hyper" && nucl != "float" && nucl != "double"
              && nucl != "char" && nucl != "string" && nucl != "type" && nucl != "any";
    assert(*entity || typeArguments->empty());
    return nucl;
}

OUString entityName(OUString const& name) {
    sal_Int32 idx = name.lastIndexOf('.');
    if (idx == -1)
        return name;

    return OUString(name.subView(idx + 1));
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
