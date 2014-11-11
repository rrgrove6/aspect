/*
  Copyright (C) 2011 - 2014 by the authors of the ASPECT code.

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


#include <aspect/material_model/melt_simple.h>
#include <deal.II/base/parameter_handler.h>

using namespace dealii;

namespace aspect
{
  namespace MaterialModel
  {
    template <int dim>
    double
    MeltSimple<dim>::
    reference_viscosity () const
    {
      return eta_0;
    }

    template <int dim>
    double
    MeltSimple<dim>::
    reference_density () const
    {
      return reference_rho_s;
    }

    template <int dim>
    bool
    MeltSimple<dim>::
    viscosity_depends_on (const NonlinearDependence::Dependence dependence) const
    {
      if ((dependence & NonlinearDependence::compositional_fields) != NonlinearDependence::none)
        return true;
      else
        return false;
    }


    template <int dim>
    bool
    MeltSimple<dim>::
    density_depends_on (const NonlinearDependence::Dependence dependence) const
    {
      if ((dependence & NonlinearDependence::temperature) != NonlinearDependence::none)
        return true;
      else
        return false;
    }

    template <int dim>
    bool
    MeltSimple<dim>::
    compressibility_depends_on (const NonlinearDependence::Dependence) const
    {
      return false;
    }

    template <int dim>
    bool
    MeltSimple<dim>::
    specific_heat_depends_on (const NonlinearDependence::Dependence) const
    {
      return false;
    }

    template <int dim>
    bool
    MeltSimple<dim>::
    thermal_conductivity_depends_on (const NonlinearDependence::Dependence dependence) const
    {
      return false;
    }


    template <int dim>
    bool
    MeltSimple<dim>::
    is_compressible () const
    {
      return false;
    }

    template <int dim>
    double
    MeltSimple<dim>::
    melting_rate (const double temperature,
                  const double pressure,
                  const std::vector<double> &composition) const
    {
      // anhydrous melting of peridotite after Katz, 2003
      const double T_solidus  = A1 + 273.15
      		                + A2 * pressure
      		                + A3 * pressure * pressure;
      const double T_lherz_liquidus = B1 + 273.15
      		                      + B2 * pressure
      		                      + B3 * pressure * pressure;
      const double T_liquidus = C1 + 273.15
      		                + C2 * pressure
      		                + C3 * pressure * pressure;

      // melt fraction for peridotite with clinopyroxene
      double peridotite_melt_fraction;
      if (temperature < T_solidus || pressure > 1.3e10)
        peridotite_melt_fraction = 0.0;
      else if (temperature > T_lherz_liquidus)
        peridotite_melt_fraction = 1.0;
      else
        peridotite_melt_fraction = std::pow((temperature - T_solidus) / (T_lherz_liquidus - T_solidus),beta);

      // melt fraction after melting of all clinopyroxene
      const double R_cpx = r1 + r2 * pressure;
      const double F_max = M_cpx / R_cpx;

      if(peridotite_melt_fraction > F_max && temperature < T_liquidus)
      {
        const double T_max = std::pow(F_max,1/beta) * (T_lherz_liquidus - T_solidus) + T_solidus;
        peridotite_melt_fraction = F_max + (1 - F_max) * pow((temperature - T_max) / (T_liquidus - T_max),beta);
      }

      if (this->get_timestep_number() < 0)
    	peridotite_melt_fraction = 0.0;


      AssertThrow(this->introspection().compositional_name_exists("peridotite"),
                  ExcMessage("Material model Melt simple only works if there is a compositional field called peridotite."));
      const unsigned int peridotite_index = this->introspection().compositional_index_for_name("peridotite");

      const double melt_fraction = (peridotite_melt_fraction > composition[peridotite_index]
                                    ?
                                    peridotite_melt_fraction - composition[peridotite_index]
                                    :
                                    0.0);

      return melt_fraction;
    }


    template <int dim>
    void
    MeltSimple<dim>::
    evaluate(const typename Interface<dim>::MaterialModelInputs &in, typename Interface<dim>::MaterialModelOutputs &out) const
    {
      for (unsigned int i=0;i<in.position.size();++i)
        {
          if (this->include_melt_transport())
            {
              const unsigned int porosity_idx = this->introspection().compositional_index_for_name("porosity");
              const unsigned int peridotite_idx = this->introspection().compositional_index_for_name("peridotite");
              const double porosity = std::max(in.composition[i][porosity_idx],0.0);
              out.viscosities[i] = eta_0 * exp(- alpha_phi * porosity);

              for (unsigned int c=0;c<in.composition[i].size();++c)
                {
                  if (c == peridotite_idx && this->get_timestep()>0)
                    out.reaction_terms[i][c] = melting_rate(in.temperature[i], in.pressure[i], in.composition[i]);
                  else if (c == porosity_idx && this->get_timestep()>0)
                	out.reaction_terms[i][c] = melting_rate(in.temperature[i], in.pressure[i], in.composition[i])
                	                           * reference_rho_s  / this->get_timestep();
                  else
                    out.reaction_terms[i][c] = 0.0;
                }
            }
          else
          {
            out.viscosities[i] = eta_0;
            for (unsigned int c=0;c<in.composition[i].size();++c)
              out.reaction_terms[i][c] = 0.0;
          }

          out.densities[i] = reference_rho_s * (1 - thermal_expansivity * (in.temperature[i] - reference_T));
          out.thermal_expansion_coefficients[i] = thermal_expansivity;
          out.specific_heat[i] = reference_specific_heat;
          out.thermal_conductivities[i] = thermal_conductivity;
          out.compressibilities[i] = 0.0;
        }
    }

    template <int dim>
    void
    MeltSimple<dim>::
    evaluate_with_melt(const typename MeltInterface<dim>::MaterialModelInputs &in, typename MeltInterface<dim>::MaterialModelOutputs &out) const
    {
      evaluate(in, out);
      const unsigned int porosity_idx = this->introspection().compositional_index_for_name("porosity");

      for (unsigned int i=0;i<in.position.size();++i)
        {
          double porosity = std::max(in.composition[i][porosity_idx],0.0);

          out.fluid_viscosities[i] = eta_f;
          out.permeabilities[i] = reference_permeability * std::pow(porosity,3) * std::pow(1.0-porosity,2);
          out.fluid_densities[i] = reference_rho_f;
          out.fluid_compressibilities[i] = 0.0;

          porosity = std::max(std::min(porosity,0.995),5e-3);
          out.compaction_viscosities[i] = eta_0 * (1.0 - porosity) / porosity;
        }
    }



    template <int dim>
    void
    MeltSimple<dim>::declare_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Material model");
      {
        prm.enter_subsection("Melt simple");
        {
          prm.declare_entry ("Reference solid density", "3000",
                             Patterns::Double (0),
                             "Reference density of the solid $\\rho_{s,0}$. Units: $kg/m^3$.");
          prm.declare_entry ("Reference melt density", "2500",
                             Patterns::Double (0),
                             "Reference density of the melt/fluid$\\rho_{f,0}$. Units: $kg/m^3$.");
          prm.declare_entry ("Reference temperature", "293",
                             Patterns::Double (0),
                             "The reference temperature $T_0$. The reference temperature is used "
                             "in both the density and viscosity formulas. Units: $K$.");
          prm.declare_entry ("Reference shear viscosity", "5e20",
                             Patterns::Double (0),
                             "The value of the constant viscosity $\\eta_0$ of the solid matrix. "
                             "This viscosity may be modified by both temperature and porosity "
                             "dependencies. Units: $Pa s$.");
          prm.declare_entry ("Reference melt viscosity", "10",
                             Patterns::Double (0),
                             "The value of the constant melt viscosity $\\eta_f$. Units: $Pa s$.");
          prm.declare_entry ("Exponential melt weakening factor", "27",
                             Patterns::Double (0),
                             "The porosity dependence of the viscosity. Units: dimensionless.");
          prm.declare_entry ("Thermal viscosity exponent", "0.0",
                             Patterns::Double (0),
                             "The temperature dependence of viscosity. Dimensionless exponent. "
                             "See the general documentation "
                             "of this model for a formula that states the dependence of the "
                             "viscosity on this factor, which is called $\\beta$ there.");
          prm.declare_entry ("Thermal conductivity", "4.7",
                             Patterns::Double (0),
                             "The value of the thermal conductivity $k$. "
                             "Units: $W/m/K$.");
          prm.declare_entry ("Reference specific heat", "1250",
                             Patterns::Double (0),
                             "The value of the specific heat $cp$. "
                             "Units: $J/kg/K$.");
          prm.declare_entry ("Thermal expansion coefficient", "2e-5",
                             Patterns::Double (0),
                             "The value of the thermal expansion coefficient $\\beta$. "
                             "Units: $1/K$.");
          prm.declare_entry ("Reference permeability", "1e-8",
                             Patterns::Double(),
                             "Reference permeability of the solid host rock."
                             "Units: $m^2$.");
          prm.declare_entry ("A1", "1085.7",
                             Patterns::Double (),
                             "Constant parameter in the quadratic "
                             "function that approximates the solidus "
                             "of peridotite. "
                             "Units: $°C$.");
          prm.declare_entry ("A2", "1.329e-7",
                             Patterns::Double (),
                             "Prefactor of the linear pressure term "
                             "in the quadratic function that approximates "
                             "the solidus of peridotite. "
                             "Units: $°C/Pa$.");
          prm.declare_entry ("A3", "-5.1e-18",
                             Patterns::Double (),
                             "Prefactor of the quadratic pressure term "
                             "in the quadratic function that approximates "
                             "the solidus of peridotite. "
                             "Units: $°C/(Pa^2)$.");
          prm.declare_entry ("B1", "1475.0",
                             Patterns::Double (),
                             "Constant parameter in the quadratic "
                             "function that approximates the lherzolite "
                             "liquidus used for calculating the fraction "
                             "of peridotite-derived melt. "
                             "Units: $°C$.");
          prm.declare_entry ("B2", "8.0e-8",
                             Patterns::Double (),
                             "Prefactor of the linear pressure term "
                             "in the quadratic function that approximates "
                             "the  lherzolite liquidus used for "
                             "calculating the fraction of peridotite-"
                             "derived melt. "
                             "Units: $°C/Pa$.");
          prm.declare_entry ("B3", "-3.2e-18",
                             Patterns::Double (),
                             "Prefactor of the quadratic pressure term "
                             "in the quadratic function that approximates "
                             "the  lherzolite liquidus used for "
                             "calculating the fraction of peridotite-"
                             "derived melt. "
                             "Units: $°C/(Pa^2)$.");
          prm.declare_entry ("C1", "1780.0",
                             Patterns::Double (),
                             "Constant parameter in the quadratic "
                             "function that approximates the liquidus "
                             "of peridotite. "
                             "Units: $°C$.");
          prm.declare_entry ("C2", "4.50e-8",
                             Patterns::Double (),
                             "Prefactor of the linear pressure term "
                             "in the quadratic function that approximates "
                             "the liquidus of peridotite. "
                             "Units: $°C/Pa$.");
          prm.declare_entry ("C3", "-2.0e-18",
                             Patterns::Double (),
                             "Prefactor of the quadratic pressure term "
                             "in the quadratic function that approximates "
                             "the liquidus of peridotite. "
                             "Units: $°C/(Pa^2)$.");
          prm.declare_entry ("r1", "0.5",
                             Patterns::Double (),
                             "Constant in the linear function that "
                             "approximates the clinopyroxene reaction "
                             "coefficient. "
                             "Units: non-dimensional.");
          prm.declare_entry ("r2", "8e-11",
                             Patterns::Double (),
                             "Prefactor of the linear pressure term "
                             "in the linear function that approximates "
                             "the clinopyroxene reaction coefficient. "
                             "Units: $1/Pa$.");
          prm.declare_entry ("beta", "1.5",
                             Patterns::Double (),
                             "Exponent of the melting temperature in "
                             "the melt fraction calculation. "
                             "Units: non-dimensional.");
          prm.declare_entry ("Peridotite melting entropy change", "300",
                             Patterns::Double (),
                             "The entropy change for the phase transition "
                             "from solid to melt of peridotite. "
                             "Units: $J/(kg K)$.");
        }
        prm.leave_subsection();
      }
      prm.leave_subsection();
    }



    template <int dim>
    void
    MeltSimple<dim>::parse_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Material model");
      {
        prm.enter_subsection("Melt simple");
        {
          reference_rho_s            = prm.get_double ("Reference solid density");
          reference_rho_f            = prm.get_double ("Reference melt density");
          reference_T                = prm.get_double ("Reference temperature");
          eta_0                      = prm.get_double ("Reference shear viscosity");
          eta_f                      = prm.get_double ("Reference melt viscosity");
          reference_permeability     = prm.get_double ("Reference permeability");
          thermal_viscosity_exponent = prm.get_double ("Thermal viscosity exponent");
          thermal_conductivity       = prm.get_double ("Thermal conductivity");
          reference_specific_heat    = prm.get_double ("Reference specific heat");
          thermal_expansivity        = prm.get_double ("Thermal expansion coefficient");
          alpha_phi                  = prm.get_double ("Exponential melt weakening factor");

          if (thermal_viscosity_exponent!=0.0 && reference_T == 0.0)
            AssertThrow(false, ExcMessage("Error: Material model Melt simple with Thermal viscosity exponent can not have reference_T=0."));

          A1              = prm.get_double ("A1");
          A2              = prm.get_double ("A2");
          A3              = prm.get_double ("A3");
          B1              = prm.get_double ("B1");
          B2              = prm.get_double ("B2");
          B3              = prm.get_double ("B3");
          C1              = prm.get_double ("C1");
          C2              = prm.get_double ("C2");
          C3              = prm.get_double ("C3");
          r1              = prm.get_double ("r1");
          r2              = prm.get_double ("r2");
          beta            = prm.get_double ("beta");
          peridotite_melting_entropy_change
                          = prm.get_double ("Peridotite melting entropy change");
        }
        prm.leave_subsection();
      }
      prm.leave_subsection();
    }
  }
}

// explicit instantiations
namespace aspect
{
  namespace MaterialModel
  {
    ASPECT_REGISTER_MATERIAL_MODEL(MeltSimple,
                                   "melt simple",
                                   "A material model that implements a simple formulation of the "
                                   "material parameters required for the modelling of melt transport, "
                                   "including a source term for the porosity according to the melting "
                                   "model for dry peridotite of Katz, 2003. ")
  }
}
