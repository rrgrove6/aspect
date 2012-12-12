/*
  Copyright (C) 2011, 2012 by the authors of the ASPECT code.

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
/*  $Id$  */


#include <aspect/termination_criteria/steady_rms_velocity.h>

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/numerics/error_estimator.h>

namespace aspect
{
  namespace TerminationCriteria
  {
    template <int dim>
    bool
    SteadyRMSVelocity<dim>::execute(void)
    {
      const QGauss<dim> quadrature_formula (this->get_fe()
                                            .base_element(0).degree+1);
      const unsigned int n_q_points = quadrature_formula.size();

      FEValues<dim> fe_values (this->get_mapping(),
                               this->get_fe(),
                               quadrature_formula,
                               update_values   |
                               update_quadrature_points |
                               update_JxW_values);
      std::vector<Tensor<1,dim> > velocity_values(n_q_points);

      double local_velocity_square_integral = 0;

      typename DoFHandler<dim>::active_cell_iterator
      cell = this->get_dof_handler().begin_active(),
      endc = this->get_dof_handler().end();
      for (; cell!=endc; ++cell)
        if (cell->is_locally_owned())
          {
            fe_values.reinit (cell);
            fe_values[this->introspection().extractors.velocities].get_function_values (this->get_solution(),
                                                                                        velocity_values);
            for (unsigned int q = 0; q < n_q_points; ++q)
              {
                local_velocity_square_integral += ((velocity_values[q] * velocity_values[q]) *
                                                   fe_values.JxW(q));
              }
          }

      const double global_velocity_square_integral
        = Utilities::MPI::sum (local_velocity_square_integral, this->get_mpi_communicator());

      // Calculate the global root mean square velocity
      const double vrms = std::sqrt(global_velocity_square_integral) / std::sqrt(this->get_volume());

      // Keep a list of times and RMS velocities at those times
      _time_rmsvel.push_back(std::make_pair(this->get_time(), vrms));

      // If the length of the simulation time covered in the list is shorter than the
      // specified parameter, we must continue the simulation
      // TODO: handle years vs. seconds
      if (_time_rmsvel.back().first - _time_rmsvel.front().first < _time_length) return false;

      // Remove old times until we're at the correct time period
      std::list<std::pair<double, double> >::iterator       it;
      it = _time_rmsvel.begin();
      while (_time_rmsvel.back().first - (*it).first > _time_length) it++;
      _time_rmsvel.erase(_time_rmsvel.begin(), it);

      // Scan through the list and calculate the min, mean and max of the RMS velocities
      double      rms_min, rms_max, rms_sum=0, rms_mean, deviation_max;
      rms_min = rms_max = _time_rmsvel.front().second;
      for (it=_time_rmsvel.begin(); it!=_time_rmsvel.end(); ++it)
        {
          rms_min = fmin(rms_min, (*it).second);
          rms_max = fmax(rms_max, (*it).second);
          rms_sum += (*it).second;
        }
      rms_mean = rms_sum/_time_rmsvel.size();

      // If the min and max are within the acceptable deviation of the mean,
      // we are in steady state and return true, otherwise return false
      deviation_max = fmax(rms_mean - rms_min, rms_max - rms_mean);

      if (deviation_max/rms_mean > _relative_deviation) return false;

      return true;
    }

    template <int dim>
    void
    SteadyRMSVelocity<dim>::declare_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Termination criteria");
      {
        prm.enter_subsection("Steady state velocity");
        {
          prm.declare_entry ("Maximum relative deviation", "0.05",
                             Patterns::Double (0),
                             "The maximum relative deviation of the RMS in recent "
                             "simulation time for the system to be considered in "
                             "steady state and the simulation terminated.");
          prm.declare_entry ("Time in steady state", "1e7",
                             Patterns::Double (0),
                             "The minimum length of simulation time that the system "
                             "should be in steady state before termination."
                             "Units: years if the "
                             "'Use years in output instead of seconds' parameter is set; "
                             "seconds otherwise.");
        }
        prm.leave_subsection ();
      }
      prm.leave_subsection ();
    }


    template <int dim>
    void
    SteadyRMSVelocity<dim>::parse_parameters (ParameterHandler &prm)
    {
      prm.enter_subsection("Termination criteria");
      {
        prm.enter_subsection("Steady state velocity");
        {
          _relative_deviation = prm.get_double ("Maximum relative deviation");
          _time_length = prm.get_double ("Time in steady state");
        }
        prm.leave_subsection ();
      }
      prm.leave_subsection ();
      AssertThrow(_relative_deviation >= 0, ExcMessage("Relative deviation must be greater than or equal to 0."));
      AssertThrow(_time_length > 0, ExcMessage("Steady state minimum time period must be greater than 0."));
    }
  }
}

// explicit instantiations
namespace aspect
{
  namespace TerminationCriteria
  {
    ASPECT_REGISTER_TERMINATION_CRITERION(SteadyRMSVelocity,
                                          "steady_rms_velocity",
                                          "A criterion that terminates the simulation when the RMS "
                                          "of the velocity field stays within a certain range for a "
                                          "specified period of time.")
  }
}