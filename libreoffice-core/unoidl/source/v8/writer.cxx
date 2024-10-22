#include <iostream>
#include <sal/types.h>
#include <regex>

#include "writer.hxx"

namespace writer {

void BaseWriter::writeDoc(OUString const& doc) {
    static std::regex starting_space("(^|\\n)[ \\t]*");
    static std::regex wrapped_text("([^\\n.]+)[\\n\\r]([^\\n.]+)");
    static std::regex unused_tags("</?(p|ol|ul)>(\\n?)");
    static std::regex triple_line("\\n\\n\\n");
    static std::regex list_tag("<li>([^<]+)</li>");
    static std::regex code_tag("<code>([^<]+)</code>");
    static std::regex var_tag("<var>([^<]+)</var>");
    static std::regex em_tag("<em>([^<]+)</em>");
    static std::regex br_tag("<(br|BR)>");
    static std::regex fused_annotation("[.](@see+)");
    static std::regex cpp_namespace("com::sun::star");
    static std::regex cpp_namespace_sep("::(\\w+)");
    static std::regex ts_without_see("( ?)(@see )?(LibreOffice.\\w+)");
    static std::regex ending_space("[ \\t\\n]*$");
    if (doc.isEmpty())
        return;

    auto doc_str = std::basic_string<char>(doc.toUtf8().getStr());
    doc_str = std::regex_replace(doc_str, starting_space, "$1$2");
    doc_str = std::regex_replace(doc_str, unused_tags, "$2\n");
    doc_str = std::regex_replace(doc_str, wrapped_text, "$1 $2");
    doc_str = std::regex_replace(doc_str, list_tag, "- $1\n\n");
    doc_str = std::regex_replace(doc_str, code_tag, "`$1`");
    doc_str = std::regex_replace(doc_str, var_tag, "`$1`");
    doc_str = std::regex_replace(doc_str, em_tag, "_$1_");
    doc_str = std::regex_replace(doc_str, wrapped_text, "$1 $2");
    doc_str = std::regex_replace(doc_str, fused_annotation, "\n$1");
    doc_str = std::regex_replace(doc_str, cpp_namespace, "LibreOffice");
    doc_str = std::regex_replace(doc_str, cpp_namespace_sep, ".$1");
    doc_str = std::regex_replace(doc_str, ts_without_see, "$1@see $3");
    doc_str = std::regex_replace(doc_str, br_tag, "  \n");
    for (int i = 0; i < 10; i++)
        doc_str = std::regex_replace(doc_str, triple_line, "\n\n");
    doc_str = std::regex_replace(doc_str, ending_space, "");

    out("\n/**\n" + OUString::fromUtf8(doc_str) + "\n */\n");
}

void BaseWriter::writeDoc(rtl::Reference<unoidl::Entity> const& entity) {
    assert(entity.is());
    writeDoc(entity->getDoc());
}

void BaseWriter::close() { file_->close(); }

void BaseWriter::writeEntity(OUString const& name) {
    std::map<OUString, Entity*>::iterator i(entities_.find(name));
    if (i == entities_.end() || !i->second->relevant)
        return;

    currentEntity_ = name;

    assert(i->second.written != Entity::Written::DEFINITION);
    i->second->written = Entity::Written::DEFINITION;

    for (auto& j : i->second->dependencies) {
        std::map<OUString, Entity*>::iterator k(entities_.find(j));
        if (k == entities_.end()) {
            writeInterfaceDependency(name, j, false);
            continue;
        }
        if (k->second->entity->getSort() != unoidl::Entity::SORT_EXCEPTION_TYPE) {
            k->second->written = Entity::Written::DECLARATION;

            writeInterfaceDependency(
                name, j,
                static_cast<unoidl::PublishableEntity*>(k->second->entity.get())->isPublished());
        }
    }
    for (auto& j : i->second->interfaceDependencies) {
        std::map<OUString, Entity*>::iterator k(entities_.find(j));
        if (k != entities_.end()) {
            k->second->written = Entity::Written::DECLARATION;

            if (k->second->entity->getSort() != unoidl::Entity::SORT_INTERFACE_TYPE) {
                std::cerr << "Entity " << j << " should be an interface type" << std::endl;
                std::exit(EXIT_FAILURE);
            }

            writeInterfaceDependency(
                name, j,
                static_cast<unoidl::PublishableEntity*>(k->second->entity.get())->isPublished());
        } else {
            writeInterfaceDependency(name, j, false);
        }
    }

    rtl::Reference<unoidl::PublishableEntity> ent(
        static_cast<unoidl::PublishableEntity*>(i->second->entity.get()));

    // write the entity doc
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

OUString BaseWriter::resolveTypedef(OUString const& type)
{
    std::size_t rank;
    std::vector<OUString> args;
    bool isEntity;
    OUString nucl(decomposeType(type, &rank, &args, &isEntity));
    if (isEntity && entities_[nucl]->entity->getSort() == unoidl::Entity::SORT_TYPEDEF) {
        OUString rank_str;
        for (size_t i = 0; i < rank; i++) {
            rank_str += "[]";
        }
        OUString fulltype
            = rank_str
              + static_cast<unoidl::TypedefEntity*>(entities_[nucl]->entity.get())->getType();
        return resolveTypedef(fulltype);
    }

    return type;
}

OUString simplifyNamespace(OUString const& name) { return name.replaceFirst("com.sun.star.", ""); }

void BaseWriter::createEntityFile(OUString const& entityName, OUString const& suffix) {
    OUString fileUrl(outDirectoryUrl_ + "/" + simplifyNamespace(entityName).replaceAll(".", "/")
                     + suffix);

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

bool BaseWriter::isEntityOfSort(OUString const& entityName, unoidl::Entity::Sort sort) {
    auto entity = entities_.find(entityName);
    return entity != entities_.end() && entity->second->entity->getSort() == sort;
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

OUString entityNamespace(OUString const& name) {
    sal_Int32 idx = name.lastIndexOf('.');
    if (idx == -1)
        return name;

    return OUString(name.subView(0, idx));
}

OUString cName(const OUString& name) { return simplifyNamespace(name).replaceAll(".", "_"); }

OUString cStructName(const OUString& name) { return "_unov8_" + cName(name); }

bool shouldSkipMethod(const unoidl::InterfaceTypeEntity::Method& method) {
    std::size_t rank;
    std::vector<OUString> args;
    bool entity;
    decomposeType(method.returnType, &rank, &args, &entity);
    bool hasPolystruct = args.size() != 0 || method.returnType.endsWith("awt.ItemListEvent");

    // TODO: instead of skipping methods with polystruct, add proper support
    if (hasPolystruct)
        return true;

    // TODO: instead of skipping methods with out/inout params, add proper support
    // TODO: instead of skipping methods with polystruct type, add proper support
    for (auto k(method.parameters.begin()); k != method.parameters.end(); ++k) {
        if (k->direction != unoidl::InterfaceTypeEntity::Method::Parameter::DIRECTION_IN) {
            return true;
        }

        decomposeType(k->type, &rank, &args, &entity);
        if (args.size() != 0 || k->type.endsWith("awt.ItemListEvent")) {
            return true;
        }
    }

    return false;
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
