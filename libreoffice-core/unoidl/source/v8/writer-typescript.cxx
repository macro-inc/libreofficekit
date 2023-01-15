#include "writer.hxx"

namespace writer {
void TypeScriptWriter::writeName(OUString const& name) {}
void TypeScriptWriter::writeType(OUString const& name) {}
void TypeScriptWriter::writeInterfaceDependency(OUString const& dependentName,
                                                OUString const& dependencyName, bool published) {}
void TypeScriptWriter::writeEnum(OUString const& name,
                                 rtl::Reference<unoidl::EnumTypeEntity> entity) {}
void TypeScriptWriter::writePlainStruct(OUString const& name,
                                        rtl::Reference<unoidl::PlainStructTypeEntity> entity) {}
void TypeScriptWriter::writePolymorphicStruct(
    OUString const& name, rtl::Reference<unoidl::PolymorphicStructTypeTemplateEntity> entity) {}
void TypeScriptWriter::writeException(OUString const& name,
                                      rtl::Reference<unoidl::ExceptionTypeEntity> entity) {}
void TypeScriptWriter::writeInterface(OUString const& name,
                                      rtl::Reference<unoidl::InterfaceTypeEntity> entity) {}
void TypeScriptWriter::writeTypedef(OUString const& name,
                                    rtl::Reference<unoidl::TypedefEntity> entity) {}
void TypeScriptWriter::writeConstantGroup(OUString const& name,
                                          rtl::Reference<unoidl::ConstantGroupEntity> entity) {}
void TypeScriptWriter::writeSingleInterfaceService(
    OUString const& name, rtl::Reference<unoidl::SingleInterfaceBasedServiceEntity> entity) {}
void TypeScriptWriter::writeAccumulationService(
    OUString const& name, rtl::Reference<unoidl::AccumulationBasedServiceEntity> entity) {}
void TypeScriptWriter::writeInterfaceSingleton(
    OUString const& name, rtl::Reference<unoidl::InterfaceBasedSingletonEntity> entity) {}
void TypeScriptWriter::writeServiceSingleton(
    OUString const& name, rtl::Reference<unoidl::ServiceBasedSingletonEntity> entity) {}
}
