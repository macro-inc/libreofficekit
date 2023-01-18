#include "writer.hxx"

namespace writer {
void V8Writer::writeName(OUString const& name) { out(simplifyNamespace(name)); }
void V8Writer::writeType(OUString const& name) {
    std::size_t rank;
    std::vector<OUString> args;
    bool entity;
    OUString nucl(decomposeType(name, &rank, &args, &entity));

    // Handle special cases of sequences
    if (rank > 0) {
        if (nucl == "byte") {
            rank--;
            nucl = "ArrayBuffer";
        } else if (nucl == "beans.PropertyValue") {
            rank--;
            nucl = "Record<string, Any>";
        }
    }

    for (std::size_t i = 0; i != rank; ++i) {
        out("Sequence<");
    }
    if (entity) {
        writeName(nucl);
    } else {
        out(nucl);
    }
    if (!args.empty()) {
        out("<");
        for (auto i(args.begin()); i != args.end(); ++i) {
            if (i != args.begin()) {
                out(", ");
            }
            writeType(*i);
        }
        out(">");
    }
    for (std::size_t i = 0; i != rank; ++i) {
        out(">");
    }
}

void V8Writer::writeInterfaceDependency(OUString const& dependentName,
                                        OUString const& dependencyName, bool published) {
    if (!published)
        return;

    // calculate the relative path for the dependency from the dependent
    OUString dependent_ = simplifyNamespace(dependentName);
    OUString dependency_ = simplifyNamespace(dependencyName);
    OUString dependencyEntity = entityName(dependencyName);

    out("import { " + dependencyEntity + "} from '");

    sal_Int32 i = 0, j = 0;
    // skip namespace components that are the same
    for (;;) {
        OUString dependentNS(dependent_.getToken(0, '.', j));
        if (j == -1)
            break;
        OUString dependencyNS = dependency_.getToken(0, '.', i);
        if (i == -1)
            break;

        if (dependencyNS != dependentNS)
            break;
    }
    bool sameDir = true;
    // for every differing component of the namespace, go up a directory
    while (j != -1) {
        dependent_.getToken(0, '.', j);
        sameDir = false;
        out("../");
    }
    if (sameDir)
        out("./");
    // get the remaining path
    while (i != -1) {
        out(dependency_.getToken(0, '.', i) + "/");
    }

    out(dependencyEntity);
    out("';");
}

void V8Writer::writeEnum(OUString const& name, rtl::Reference<unoidl::EnumTypeEntity> entity) {
    out("export declare enum " + entityName(name) + "{");
    for (auto& m : entity->getMembers()) {
        writeDoc(m.doc);
        out(m.name + ",");
    }
    out("}");
}

void V8Writer::writePlainStruct(OUString const& name,
                                rtl::Reference<unoidl::PlainStructTypeEntity> entity) {
    auto base = entity->getDirectBase();
    out("export interface " + name);
    if (!base.isEmpty())
        out(" extends " + base);

    out("{");

    for (auto& m : entity->getDirectMembers()) {
        writeDoc(m.doc);
        out(m.name + ",");
    }
    out("}");
}

void V8Writer::writePolymorphicStruct(
    OUString const& name, rtl::Reference<unoidl::PolymorphicStructTypeTemplateEntity> entity) {
    out("export type " + name + "<");
    auto& typeParams = entity->getTypeParameters();
    for (auto i(typeParams.begin()); i != typeParams.end(); ++i) {
        if (i != typeParams.begin()) {
            out(",");
        }
        out(*i);
    }
    out("> = {");

    for (auto& j : entity->getMembers()) {
        writeDoc(j.doc);
        out(j.name + ": ");

        if (j.parameterized) {
            out(j.type);
        } else {
            writeType(j.type);
        }
        out(";");
    }
    out("};");
}

void V8Writer::writeException(OUString const& name,
                              rtl::Reference<unoidl::ExceptionTypeEntity> entity) {
    out("// exception " + name);
}

void V8Writer::writeInterface(OUString const& name,
                              rtl::Reference<unoidl::InterfaceTypeEntity> entity) {
    out("export interface " + name);
    auto& bases = entity->getDirectMandatoryBases();
    if (!bases.empty())
        out(" extends ");
    for (auto i(bases.begin()); i != bases.end(); ++i) {
        if (i != bases.begin()) {
            out(",");
        }
        out(i->name);
    }
    out("{");

    for (auto& i : entity->getDirectOptionalBases()) {
        out("as(");

        out("): | undefined;");
    }
    out("}");
}

void V8Writer::writeTypedef(OUString const& name, rtl::Reference<unoidl::TypedefEntity> entity) {}
void V8Writer::writeConstantGroup(OUString const& name,
                                  rtl::Reference<unoidl::ConstantGroupEntity> entity) {}
void V8Writer::writeSingleInterfaceService(
    OUString const& name, rtl::Reference<unoidl::SingleInterfaceBasedServiceEntity> entity) {}
void V8Writer::writeAccumulationService(
    OUString const& name, rtl::Reference<unoidl::AccumulationBasedServiceEntity> entity) {}
void V8Writer::writeInterfaceSingleton(
    OUString const& name, rtl::Reference<unoidl::InterfaceBasedSingletonEntity> entity) {}
void V8Writer::writeServiceSingleton(OUString const& name,
                                     rtl::Reference<unoidl::ServiceBasedSingletonEntity> entity) {}
}
