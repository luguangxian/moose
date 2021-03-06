//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

// MOOSE includes
#include "MaterialOutputAction.h"
#include "FEProblem.h"
#include "MooseApp.h"
#include "AddOutputAction.h"
#include "Material.h"
#include "RankTwoTensor.h"
#include "RankFourTensor.h"

// Declare the output helper specializations
template <>
void MaterialOutputAction::materialOutputHelper<Real>(const std::string & material_name,
                                                      std::shared_ptr<Material> material);

template <>
void
MaterialOutputAction::materialOutputHelper<RealVectorValue>(const std::string & material_name,
                                                            std::shared_ptr<Material> material);

template <>
void
MaterialOutputAction::materialOutputHelper<RealTensorValue>(const std::string & material_name,
                                                            std::shared_ptr<Material> material);

template <>
void MaterialOutputAction::materialOutputHelper<RankTwoTensor>(const std::string & material_name,
                                                               std::shared_ptr<Material> material);

template <>
void MaterialOutputAction::materialOutputHelper<RankFourTensor>(const std::string & material_name,
                                                                std::shared_ptr<Material> material);

registerMooseAction("MooseApp", MaterialOutputAction, "setup_material_output");

template <>
InputParameters
validParams<MaterialOutputAction>()
{
  InputParameters params = validParams<Action>();
  return params;
}

MaterialOutputAction::MaterialOutputAction(InputParameters params)
  : Action(params), _output_warehouse(_app.getOutputWarehouse())
{
}

void
MaterialOutputAction::act()
{
  // Error if _problem is NULL, I don't know how this would happen
  if (_problem.get() == NULL)
    mooseError("FEProblemBase pointer is NULL, it is needed for auto material property output");

  buildMaterialOutputObjects(_problem.get());
}

void
MaterialOutputAction::buildMaterialOutputObjects(FEProblemBase * problem_ptr)
{
  // Do nothing if the application does not have output
  if (!_app.actionWarehouse().hasActions("add_output"))
    return;

  // Set the pointers to the MaterialData objects (Note, these pointers are not available at
  // construction)
  _block_material_data = problem_ptr->getMaterialData(Moose::BLOCK_MATERIAL_DATA);
  _boundary_material_data = problem_ptr->getMaterialData(Moose::BOUNDARY_MATERIAL_DATA);

  // A complete list of all Material objects
  const std::vector<std::shared_ptr<Material>> & materials =
      problem_ptr->getMaterialWarehouse().getObjects();

  // Handle setting of material property output in [Outputs] sub-blocks
  // Output objects can enable material property output, the following code examines the parameters
  // for each Output object and sets a flag if any Output object has output set and also builds a
  // list if the
  // properties are limited via the 'show_material_properties' parameters
  bool outputs_has_properties = false;
  std::set<std::string> output_object_properties;

  const auto & output_actions = _app.actionWarehouse().getActionListByName("add_output");
  for (const auto & act : output_actions)
  {
    // Extract the Output action
    AddOutputAction * action = dynamic_cast<AddOutputAction *>(act);
    mooseAssert(action != NULL, "No AddOutputAction with the name " << act->name() << " exists");

    // Add the material property names from the output object parameters to the list of properties
    // to output
    InputParameters & params = action->getObjectParams();
    if (params.isParamValid("output_material_properties") &&
        params.get<bool>("output_material_properties"))
    {
      outputs_has_properties = true;
      std::vector<std::string> prop_names =
          params.get<std::vector<std::string>>("show_material_properties");
      output_object_properties.insert(prop_names.begin(), prop_names.end());
    }
  }

  // Loop through each material object
  for (const auto & mat : materials)
  {
    // Extract the names of the output objects to which the material properties will be exported
    std::set<OutputName> outputs = mat->getOutputs();

    // Extract the property names that will actually be output
    std::vector<std::string> output_properties =
        mat->getParam<std::vector<std::string>>("output_properties");

    // Append the properties listed in the Outputs block
    if (outputs_has_properties)
      output_properties.insert(output_properties.end(),
                               output_object_properties.begin(),
                               output_object_properties.end());

    // Clear the list of variable names for the current material object, this list will be populated
    // with all the
    // variables names for the current material object and is needed for purposes of controlling the
    // which output objects
    // show the material property data
    _material_variable_names.clear();

    // Create necessary outputs for the properties if:
    //   (1) The Outputs block has material output enabled
    //   (2) If the Material object itself has set the 'outputs' parameter
    if (outputs_has_properties || outputs.find("none") == outputs.end())
    {
      // Add the material property for output if the name is contained in the 'output_properties'
      // list
      // or if the list is empty (all properties)
      const std::set<std::string> names = mat->getSuppliedItems();
      for (const auto & name : names)
      {
        // Add the material property for output
        if (output_properties.empty() ||
            std::find(output_properties.begin(), output_properties.end(), name) !=
                output_properties.end())
        {
          if (hasProperty<Real>(name))
            materialOutputHelper<Real>(name, mat);

          else if (hasProperty<RealVectorValue>(name))
            materialOutputHelper<RealVectorValue>(name, mat);

          else if (hasProperty<RealTensorValue>(name))
            materialOutputHelper<RealTensorValue>(name, mat);

          else if (hasProperty<RankTwoTensor>(name))
            materialOutputHelper<RankTwoTensor>(name, mat);

          else if (hasProperty<RankFourTensor>(name))
            materialOutputHelper<RankFourTensor>(name, mat);

          else
            mooseWarning("The type for material property '",
                         name,
                         "' is not supported for automatic output.");
        }

        // If the material object as limited outputs, store the variables associated with the output
        // objects
        if (!outputs.empty())
          for (const auto & output_name : outputs)
            _material_variable_names_map[output_name].insert(_material_variable_names.begin(),
                                                             _material_variable_names.end());
      }
    }
  }

  // Create the AuxVariables
  FEType fe_type(
      CONSTANT,
      MONOMIAL); // currently only elemental variables are support for material property output
  for (const auto & var_name : _variable_names)
    problem_ptr->addAuxVariable(var_name, fe_type);

  // When a Material object has 'output_properties' defined all other properties not listed must be
  // added to
  // the hide list for the output objects so that properties that are not desired do not appear.
  for (const auto & it : _material_variable_names_map)
  {
    std::set<std::string> hide;
    std::set_difference(_variable_names.begin(),
                        _variable_names.end(),
                        it.second.begin(),
                        it.second.end(),
                        std::inserter(hide, hide.begin()));

    _output_warehouse.addInterfaceHideVariables(it.first, hide);
  }
}

std::shared_ptr<MooseObjectAction>
MaterialOutputAction::createAction(const std::string & type,
                                   const std::string & property_name,
                                   const std::string & variable_name,
                                   std::shared_ptr<Material> material)
{
  // Append the list of variables to create
  _variable_names.insert(variable_name);

  // Append the list of output variables for the current material
  _material_variable_names.insert(variable_name);

  // Generate the name
  std::ostringstream name;
  name << material->name() << "_" << variable_name;

  // Set the action parameters
  InputParameters action_params = _action_factory.getValidParams("AddKernelAction");
  action_params.set<std::string>("type") = type;
  action_params.set<ActionWarehouse *>("awh") = &_awh;
  action_params.set<std::string>("task") = "add_aux_kernel";

  // Create the action
  std::shared_ptr<MooseObjectAction> action = std::static_pointer_cast<MooseObjectAction>(
      _action_factory.create("AddKernelAction", name.str(), action_params));

  // Set the object parameters
  InputParameters & object_params = action->getObjectParams();
  object_params.set<MaterialPropertyName>("property") = property_name;
  object_params.set<AuxVariableName>("variable") = variable_name;
  object_params.set<ExecFlagEnum>("execute_on") = EXEC_TIMESTEP_END;

  if (material->boundaryRestricted())
    object_params.set<std::vector<BoundaryName>>("boundary") = material->boundaryNames();
  else
    object_params.set<std::vector<SubdomainName>>("block") = material->blocks();

  // Return the created action
  return action;
}

template <>
void
MaterialOutputAction::materialOutputHelper<Real>(const std::string & property_name,
                                                 std::shared_ptr<Material> material)
{
  _awh.addActionBlock(createAction("MaterialRealAux", property_name, property_name, material));
}

template <>
void
MaterialOutputAction::materialOutputHelper<RealVectorValue>(const std::string & property_name,
                                                            std::shared_ptr<Material> material)
{
  char suffix[3] = {'x', 'y', 'z'};

  for (unsigned int i = 0; i < LIBMESH_DIM; ++i)
  {
    std::ostringstream oss;
    oss << property_name << "_" << suffix[i];

    std::shared_ptr<MooseObjectAction> action = std::static_pointer_cast<MooseObjectAction>(
        createAction("MaterialRealVectorValueAux", property_name, oss.str(), material));
    action->getObjectParams().set<unsigned int>("component") = i;
    _awh.addActionBlock(action);
  }
}

template <>
void
MaterialOutputAction::materialOutputHelper<RealTensorValue>(const std::string & property_name,
                                                            std::shared_ptr<Material> material)
{
  for (unsigned int i = 0; i < LIBMESH_DIM; ++i)
    for (unsigned int j = 0; j < LIBMESH_DIM; ++j)
    {
      std::ostringstream oss;
      oss << property_name << "_" << i << j;

      std::shared_ptr<MooseObjectAction> action = std::static_pointer_cast<MooseObjectAction>(
          createAction("MaterialRealTensorValueAux", property_name, oss.str(), material));
      action->getObjectParams().set<unsigned int>("row") = i;
      action->getObjectParams().set<unsigned int>("column") = j;
      _awh.addActionBlock(action);
    }
}

template <>
void
MaterialOutputAction::materialOutputHelper<RankTwoTensor>(const std::string & property_name,
                                                          std::shared_ptr<Material> material)
{
  for (unsigned int i = 0; i < LIBMESH_DIM; ++i)
    for (unsigned int j = 0; j < LIBMESH_DIM; ++j)
    {
      std::ostringstream oss;
      oss << property_name << "_" << i << j;

      std::shared_ptr<MooseObjectAction> action = std::static_pointer_cast<MooseObjectAction>(
          createAction("MaterialRankTwoTensorAux", property_name, oss.str(), material));
      action->getObjectParams().set<unsigned int>("i") = i;
      action->getObjectParams().set<unsigned int>("j") = j;
      _awh.addActionBlock(action);
    }
}

template <>
void
MaterialOutputAction::materialOutputHelper<RankFourTensor>(const std::string & property_name,
                                                           std::shared_ptr<Material> material)
{
  for (unsigned int i = 0; i < LIBMESH_DIM; ++i)
    for (unsigned int j = 0; j < LIBMESH_DIM; ++j)
      for (unsigned int k = 0; k < LIBMESH_DIM; ++k)
        for (unsigned int l = 0; l < LIBMESH_DIM; ++l)
        {
          std::ostringstream oss;
          oss << property_name << "_" << i << j << k << l;

          std::shared_ptr<MooseObjectAction> action = std::static_pointer_cast<MooseObjectAction>(
              createAction("MaterialRankFourTensorAux", property_name, oss.str(), material));
          action->getObjectParams().set<unsigned int>("i") = i;
          action->getObjectParams().set<unsigned int>("j") = j;
          action->getObjectParams().set<unsigned int>("k") = k;
          action->getObjectParams().set<unsigned int>("l") = l;
          _awh.addActionBlock(action);
        }
}
