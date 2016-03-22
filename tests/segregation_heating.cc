#include <aspect/fluid_pressure_boundary_conditions/interface.h>
#include <aspect/simulator_access.h>
#include <aspect/global.h>
#include <aspect/melt.h>

namespace aspect
{

  template <int dim>
  class PressureBdry:
    public FluidPressureBoundaryConditions::Interface<dim>, public ::aspect::SimulatorAccess<dim>
  {
    public:
      virtual
      void fluid_pressure_gradient (
        const MaterialModel::MaterialModelInputs<dim> &material_model_inputs,
        const MaterialModel::MaterialModelOutputs<dim> &material_model_outputs,
        std::vector<Tensor<1,dim> > &output
      ) const
      {
        const MaterialModel::MeltOutputs<dim> *melt_outputs = material_model_outputs.template get_additional_output<MaterialModel::MeltOutputs<dim> >();
        Assert(melt_outputs != NULL, ExcMessage("Need MeltOutputs from the material model for shear heating with melt."));

        for (unsigned int q=0; q<output.size(); ++q)
          {
            const double density = material_model_outputs.densities[q];
            const double melt_density = melt_outputs->fluid_densities[q];
            const double porosity = material_model_inputs.composition[q][this->introspection().compositional_index_for_name("porosity")];

            const double bulk_density = (porosity * melt_density + (1.0 - porosity) * density);
            output[q] = this->get_gravity_model().gravity_vector(material_model_inputs.position[q]) * bulk_density;
          }
      }

  };

}

// explicit instantiations
namespace aspect
{
  ASPECT_REGISTER_FLUID_PRESSURE_BOUNDARY_CONDITIONS(PressureBdry,
                                                     "PressureBdry",
                                                     "A fluid pressure boundary condition that prescribes the "
                                                     "gradient of the fluid pressure at the boundaries as "
                                                     "calculated in the analytical solution. ")

}