#include "writer.hxx"

namespace writer {
void V8Writer::writeName(OUString const& name) {}
void V8Writer::writeType(OUString const& name) {}
void V8Writer::writeInterfaceDependency(OUString const& dependentName,
                                                OUString const& dependencyName, bool published) {}
void V8Writer::writeEnum(OUString const& name, rtl::Reference<unoidl::EnumTypeEntity> entity) {}
void V8Writer::writePlainStruct(OUString const& name,
                                rtl::Reference<unoidl::PlainStructTypeEntity> entity) {}
void V8Writer::writePolymorphicStruct(
    OUString const& name, rtl::Reference<unoidl::PolymorphicStructTypeTemplateEntity> entity) {}
void V8Writer::writeException(OUString const& name,
                              rtl::Reference<unoidl::ExceptionTypeEntity> entity) {}
void V8Writer::writeInterface(OUString const& name,
                              rtl::Reference<unoidl::InterfaceTypeEntity> entity) {}
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
