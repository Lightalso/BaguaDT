// Adapted from libuipc stable_neo_hookean (Apache-2.0); local LogNeoHookean extension.
#include <uipc/constitution/log_neo_hookean.h>
#include <uipc/builtin/constitution_uid_auto_register.h>
#include <uipc/builtin/attribute_name.h>
#include <uipc/builtin/constitution_type.h>
#include <uipc/constitution/conversion.h>
#include <uipc/common/log.h>

namespace uipc::constitution
{
REGISTER_CONSTITUTION_UIDS()
{
    using namespace uipc::builtin;
    list<UIDInfo> uids;
    uids.push_back(UIDInfo{.uid  = 10010,
                           .name = "LogNeoHookean",
                           .type = string{builtin::FiniteElement}});
    return uids;
}

LogNeoHookean::LogNeoHookean(const Json& config) noexcept
    : m_config(config)
{
}

void LogNeoHookean::apply_to(geometry::SimplicialComplex& sc,
                                const ElasticModuli&         moduli,
                                Float                        mass_density) const
{
    Base::apply_to(sc, mass_density);

    auto mu     = moduli.mu();
    auto lambda = moduli.lambda();

    auto snh_mu = mu;
    auto snh_lambda = lambda;

    UIPC_ASSERT_THROW(sc.dim() == 3, "LogNeoHookean only supports 3D simplicial complex");

    auto mu_attr = sc.tetrahedra().find<Float>("mu");
    if(!mu_attr)
        mu_attr = sc.tetrahedra().create<Float>("mu", snh_mu);
    std::ranges::fill(geometry::view(*mu_attr), snh_mu);

    auto lambda_attr = sc.tetrahedra().find<Float>("lambda");
    if(!lambda_attr)
        lambda_attr = sc.tetrahedra().create<Float>("lambda", snh_lambda);
    std::ranges::fill(geometry::view(*lambda_attr), snh_lambda);
}

Json LogNeoHookean::default_config() noexcept
{
    return Json::object();
}

U64 LogNeoHookean::get_uid() const noexcept
{
    return 10010;
}
}  // namespace uipc::constitution
