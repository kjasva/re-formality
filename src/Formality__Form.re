module Validation = Formality__Validation;

module Strategy = Formality__Strategy;

module Utils = Formality__Utils;

module type Config = {
  type field;
  type state;
  type message;
  let get: (field, state) => Validation.value;
  let update: ((field, Validation.value), state) => state;
  let strategy: Strategy.t;
  type validators;
  let validators: validators;
  module Validators: {
    let find:
      (field, validators) => Validation.validator(field, state, message);
    let fold:
      (
        (field, Validation.validator(field, state, message), 'a) => 'a,
        validators,
        'a
      ) =>
      'a;
  };
};

module Make = (Form: Config) => {
  module FieldsSetOrigin =
    Set.Make(
      {
        type t = Form.field;
        let compare = Utils.comparator;
      }
    );
  module FieldsSet = {
    type t = FieldsSetOrigin.t;
    let empty = FieldsSetOrigin.empty;
    let mem = FieldsSetOrigin.mem;
    let add = FieldsSetOrigin.add;
    let remove = FieldsSetOrigin.remove;
  };
  module ResultsMapOrigin =
    Map.Make(
      {
        type t = Form.field;
        let compare = Utils.comparator;
      }
    );
  module ResultsMap = {
    type key = ResultsMapOrigin.key;
    type t = ResultsMapOrigin.t(option(Validation.result(Form.message)));
    let empty = ResultsMapOrigin.empty;
    let add = ResultsMapOrigin.add;
    let get = (key: key, map: t) =>
      switch (map |> ResultsMapOrigin.find(key)) {
      | result => result
      | exception Not_found => None
      };
  };
  type state = {
    data: Form.state,
    results: ResultsMap.t,
    submitting: bool,
    submittedOnce: bool,
    emittedFields: FieldsSet.t
  };
  type action =
    | Change((Form.field, Validation.value))
    | Blur((Form.field, Validation.value))
    | Submit
    | Reset
    | HandleSubmissionError;
  type interface = {
    state: Form.state,
    results: Form.field => option(Validation.result(Form.message)),
    submitting: bool,
    change: (Form.field, ReactEventRe.Form.t) => unit,
    blur: (Form.field, ReactEventRe.Focus.t) => unit,
    submit: ReactEventRe.Form.t => unit
  };
  let getInitialState = data => {
    data,
    results: ResultsMap.empty,
    submitting: false,
    submittedOnce: false,
    emittedFields: FieldsSet.empty
  };
  let getValidator = field =>
    switch (Form.validators |> Form.Validators.find(field)) {
    | validator => Some(validator)
    | exception Not_found => None
    };
  let getStrategy =
      (validator: Validation.validator(Form.field, Form.state, Form.message)) =>
    validator.strategy |> Js.Option.getWithDefault(Form.strategy);
  let validateDependents = (~data, ~results, ~emittedFields, dependents) =>
    dependents
    |> List.fold_left(
         ((results', emittedFields'), field') => {
           let validator = field' |> getValidator;
           let emitted = emittedFields |> FieldsSet.mem(field');
           switch (validator, emitted) {
           | (None, _)
           | (_, false) => (results', emittedFields')
           | (Some(validator), true) =>
             let result = data |> validator.validate(data |> Form.get(field'));
             (
               results' |> ResultsMap.add(field', Some(result)),
               emittedFields' |> FieldsSet.add(field')
             );
           };
         },
         (results, emittedFields)
       );
  let ifResult = (~valid, ~invalid, result) =>
    switch result {
    | Validation.Valid(true) => result |> valid
    | Validation.ValidityBag(validity) when validity.valid => result |> valid
    | Validation.Valid(false)
    | Validation.ValidityBag(_) => result |> invalid
    };
  let component = ReasonReact.reducerComponent("FormalityForm");
  let make =
      (
        ~initialState: Form.state,
        ~onSubmit: (Form.state, Validation.notifiers) => unit,
        children
      ) => {
    ...component,
    initialState: () => getInitialState(initialState),
    reducer: (action, state) =>
      switch action {
      | Change((field, value)) =>
        let data = state.data |> Form.update((field, value));
        switch (field |> getValidator) {
        | None => ReasonReact.Update({...state, data})
        | Some(validator) =>
          let strategy = validator |> getStrategy;
          let emitted = state.emittedFields |> FieldsSet.mem(field);
          switch (strategy, emitted, state.submittedOnce) {
          | (_, true, _)
          | (_, _, true)
          | (Strategy.OnFirstChange, false, false) =>
            switch validator.dependents {
            | Some(dependents) =>
              let result = data |> validator.validate(value);
              let (results, emittedFields) =
                dependents
                |> validateDependents(
                     ~data,
                     ~results=state.results,
                     ~emittedFields=state.emittedFields
                   );
              ReasonReact.Update({
                ...state,
                data,
                results: results |> ResultsMap.add(field, Some(result)),
                emittedFields: emittedFields |> FieldsSet.add(field)
              });
            | None =>
              let result = data |> validator.validate(value);
              ReasonReact.Update({
                ...state,
                data,
                results: state.results |> ResultsMap.add(field, Some(result)),
                emittedFields: state.emittedFields |> FieldsSet.add(field)
              });
            }
          | (
              Strategy.OnFirstSuccess | Strategy.OnFirstSuccessOrFirstBlur,
              false,
              false
            ) =>
            data
            |> validator.validate(value)
            |> ifResult(
                 ~valid=
                   result =>
                     switch validator.dependents {
                     | Some(dependents) =>
                       let (results, emittedFields) =
                         validateDependents(
                           ~data,
                           ~results=state.results,
                           ~emittedFields=state.emittedFields,
                           dependents
                         );
                       ReasonReact.Update({
                         ...state,
                         data,
                         results:
                           results |> ResultsMap.add(field, Some(result)),
                         emittedFields: emittedFields |> FieldsSet.add(field)
                       });
                     | None =>
                       ReasonReact.Update({
                         ...state,
                         data,
                         results:
                           state.results |> ResultsMap.add(field, Some(result)),
                         emittedFields:
                           state.emittedFields |> FieldsSet.add(field)
                       })
                     },
                 ~invalid=
                   (_) =>
                     switch validator.dependents {
                     | Some(dependents) =>
                       let (results, emittedFields) =
                         validateDependents(
                           ~data,
                           ~results=state.results,
                           ~emittedFields=state.emittedFields,
                           dependents
                         );
                       ReasonReact.Update({
                         ...state,
                         data,
                         results,
                         emittedFields
                       });
                     | None => ReasonReact.Update({...state, data})
                     }
               )
          | (Strategy.OnFirstBlur | Strategy.OnSubmit, false, false) =>
            ReasonReact.Update({...state, data})
          };
        };
      | Blur((field, value)) =>
        let validator = field |> getValidator;
        let emitted = state.emittedFields |> FieldsSet.mem(field);
        switch (validator, emitted) {
        | (None, _)
        | (Some(_), true) => ReasonReact.NoUpdate
        | (Some(validator), false) =>
          switch (validator |> getStrategy) {
          | Strategy.OnFirstChange
          | Strategy.OnFirstSuccess
          | Strategy.OnSubmit => ReasonReact.NoUpdate
          | Strategy.OnFirstBlur
          | Strategy.OnFirstSuccessOrFirstBlur =>
            let result = state.data |> validator.validate(value);
            ReasonReact.Update({
              ...state,
              results: state.results |> ResultsMap.add(field, Some(result)),
              emittedFields: state.emittedFields |> FieldsSet.add(field)
            });
          }
        };
      | Submit =>
        let (valid, results) =
          (true, state.results)
          |> Form.Validators.fold(
               (field', validator', (valid', results')) => {
                 let result =
                   state.data
                   |> validator'.validate(state.data |> Form.get(field'));
                 let results =
                   results' |> ResultsMap.add(field', Some(result));
                 switch (valid', result) {
                 | (false, _) => (false, results)
                 | (true, Validation.Valid(valid)) => (valid, results)
                 | (true, Validation.ValidityBag(validity)) => (
                     validity.valid,
                     results
                   )
                 };
               },
               Form.validators
             );
        valid ?
          ReasonReact.UpdateWithSideEffects(
            {...state, results, submitting: true, submittedOnce: true},
            (
              ({state, send}) =>
                onSubmit(
                  state.data,
                  {
                    onSuccess: () => send(Reset),
                    onFailure: () => send(HandleSubmissionError)
                  }
                )
            )
          ) :
          ReasonReact.Update({
            ...state,
            results,
            submitting: false,
            submittedOnce: true
          });
      | Reset => ReasonReact.Update(initialState |> getInitialState)
      | HandleSubmissionError =>
        ReasonReact.Update({...state, submitting: false})
      },
    render: ({state, send}) =>
      children({
        state: state.data,
        results: field => state.results |> ResultsMap.get(field),
        submitting: state.submitting,
        change: (field, event) =>
          send(Change((field, event |> Utils.formEventTargetValue))),
        blur: (field, event) =>
          send(Blur((field, event |> Utils.focusEventTargetValue))),
        submit: event => {
          if (! ReactEventRe.Form.defaultPrevented(event)) {
            event |> ReactEventRe.Form.preventDefault;
          };
          send(Submit);
        }
      })
  };
};
