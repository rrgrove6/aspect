/*
  Copyright (C) 2016 by the authors of the ASPECT code.

  This file is part of ASPECT.

  ASPECT is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  ASPECT is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with ASPECT; see the file doc/COPYING.  If not see
  <http://www.gnu.org/licenses/>.
 */


#ifndef __aspect__melt_h
#define __aspect__melt_h

#include <aspect/simulator_access.h>
#include <aspect/global.h>
#include <aspect/assembly.h>
#include <aspect/material_model/interface.h>

namespace aspect
{
  using namespace dealii;

  namespace MaterialModel
  {
    template <int dim>
    class MeltOutputs : public AdditionalMaterialOutputs<dim>
    {
      public:
        MeltOutputs (const unsigned int n_points,
                     const unsigned int n_comp)
        {
          compaction_viscosities.resize(n_points);
          fluid_viscosities.resize(n_points);
          permeabilities.resize(n_points);
          fluid_densities.resize(n_points);
          fluid_density_gradients.resize(n_points, Tensor<1,dim>());
        }

        /**
         * Compaction viscosity values $\xi$ at the given positions.
         * This parameter describes the resistance of the solid matrix
         * in a two-phase simulation to dilation and compaction.
         */
        std::vector<double> compaction_viscosities;

        /**
         * Fluid (melt) viscosity values $\eta_f$ at the given positions.
         */
        std::vector<double> fluid_viscosities;

        /**
         * Permeability values $k$ at the given positions.
         */
        std::vector<double> permeabilities;

        /**
         * Fluid (melt) density values $\rho_f$ at the given positions.
         */
        std::vector<double> fluid_densities;

        /**
         * An approximation for the fluid (melt) density gradients
         * $\nabla \rho_f$ at the given positions. These values are
         * required for compressible models to describe volume changes
         * of melt in dependence of pressure, temperature etc.
         */
        std::vector<Tensor<1,dim> > fluid_density_gradients;

        void average (const MaterialAveraging::AveragingOperation operation,
                      const FullMatrix<double>  &projection_matrix,
                      const FullMatrix<double>  &expansion_matrix)
        {
          average_property (operation, projection_matrix, expansion_matrix,
                            compaction_viscosities);
          average_property (operation, projection_matrix, expansion_matrix,
                            fluid_viscosities);
          average_property (operation, projection_matrix, expansion_matrix,
                            permeabilities);
          average_property (operation, projection_matrix, expansion_matrix,
                            fluid_densities);

          // the fluid density gradients are unfortunately stored in reverse
          // indexing. it's also not quite clear whether these should
          // really be averaged, so avoid this for now
        }
    };

  }

  /**
   * Creates additional material model output object that are
   * needed for a simulation, and attaches a pointer to them to the
   * corresponding vector in the MaterialModel::MaterialModelOutputs
   * structure.
   */
  template <int dim>
  void create_melt_material_outputs(MaterialModel::MaterialModelOutputs<dim> &output);


  namespace Assemblers
  {
    /**
     * A class for the definition of functions that implement the
     * linear system terms for the *melt* migration compressible or
     * incompressible equations.
     */
    template <int dim>
    class MeltEquations : public aspect::internal::Assembly::Assemblers::AssemblerBase<dim>,
      public SimulatorAccess<dim>
    {
      public:
        /**
         * Attaches melt outputs.
         */
        virtual
        void
        create_additional_material_model_outputs(MaterialModel::MaterialModelOutputs<dim> &outputs);


        /**
         * Compute the integrals for the preconditioner for the Stokes system in
         * the case of melt migration on a single cell.
         */
        void
        local_assemble_stokes_preconditioner_melt (const double                                             pressure_scaling,
                                                   internal::Assembly::Scratch::StokesPreconditioner<dim>  &scratch,
                                                   internal::Assembly::CopyData::StokesPreconditioner<dim> &data) const;

        /**
         * Compute the integrals for the Stokes matrix and right hand side in
         * the case of melt migration on a single cell.
         */
        void
        local_assemble_stokes_system_melt (const typename DoFHandler<dim>::active_cell_iterator &cell,
                                           const double                                     pressure_scaling,
                                           const bool                                       rebuild_stokes_matrix,
                                           internal::Assembly::Scratch::StokesSystem<dim>  &scratch,
                                           internal::Assembly::CopyData::StokesSystem<dim> &data) const;

        /**
         * Compute the boundary integrals for the Stokes right hand side in
         * the case of melt migration on a single cell. These boundary terms
         * are used to describe Neumann boundary conditions for the fluid pressure.
         */
        void
        local_assemble_stokes_system_melt_boundary (const typename DoFHandler<dim>::active_cell_iterator &cell,
                                                    const unsigned int                                    face_no,
                                                    const double                                          pressure_scaling,
                                                    internal::Assembly::Scratch::StokesSystem<dim>       &scratch,
                                                    internal::Assembly::CopyData::StokesSystem<dim>      &data) const;

        /**
         * Compute the integrals for the Advection system matrix and right hand side in
         * the case of melt migration on a single cell.
         */
        void
        local_assemble_advection_system_melt (const typename Simulator<dim>::AdvectionField      &advection_field,
                                              const double                                        artificial_viscosity,
                                              internal::Assembly::Scratch::AdvectionSystem<dim>  &scratch,
                                              internal::Assembly::CopyData::AdvectionSystem<dim> &data) const;

        /**
         * Compute the residual of the advection system on a single cell in
         * the case of melt migration.
         */
        std::vector<double>
        compute_advection_system_residual_melt(const typename Simulator<dim>::AdvectionField     &advection_field,
                                               internal::Assembly::Scratch::AdvectionSystem<dim> &scratch) const;


      private:
        /**
         * Returns the right hand side of the fluid pressure equation in
         * the case of melt migration for a single quadrature point.
         */
        double
        compute_fluid_pressure_RHS(const internal::Assembly::Scratch::StokesSystem<dim>  &scratch,
                                   MaterialModel::MaterialModelInputs<dim> &material_model_inputs,
                                   MaterialModel::MaterialModelOutputs<dim> &material_model_outputs,
                                   const unsigned int q_point) const;

        /**
         * Compute the right-hand side for the advection system index. This is
         * 0 for the temperature and all of the compositional fields, except for
         * the porosity. It includes the melting rate and a term dependent
         * on the density and velocity.
         *
         * This function is implemented in
         * <code>source/simulator/assembly.cc</code>.
         */
        double
        compute_melting_RHS(const internal::Assembly::Scratch::AdvectionSystem<dim>  &scratch,
                            typename MaterialModel::Interface<dim>::MaterialModelInputs &material_model_inputs,
                            typename MaterialModel::Interface<dim>::MaterialModelOutputs &material_model_outputs,
                            const typename Simulator<dim>::AdvectionField &advection_field,
                            const unsigned int q_point) const;
    };

    /**
     * A namespace for the definition of functions that implement various
     * other terms that need to occasionally or always be assembled.
     */
    namespace OtherTerms
    {
      /**
       * Integrate the local fluid pressure shape functions on a single cell
       * for models with melt migration, so that they can later be used to do
       * the pressure right-hand side compatibility modification.
       */
      template <int dim>
      void
      pressure_rhs_compatibility_modification_melt (const SimulatorAccess<dim>                      &simulator_access,
                                                    internal::Assembly::Scratch::StokesSystem<dim>  &scratch,
                                                    internal::Assembly::CopyData::StokesSystem<dim> &data);
    }
  }

  namespace Melt
  {
    /**
     * Compute fluid velocity and solid pressure in this ghosted solution vector.
     * @param solution
     */
    template <int dim>
    void compute_melt_variables(SimulatorAccess<dim>       &sim,
                                LinearAlgebra::BlockVector &solution);
  }
}

#endif