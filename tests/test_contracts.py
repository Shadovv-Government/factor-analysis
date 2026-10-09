"""Malformed exchange records must not satisfy the public contract."""
import json
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource
from sprint1.run import run
from test_model import ROOT, request


class ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.req_schema=json.loads((ROOT/'contracts/request.schema.json').read_text())
        cls.res_schema=json.loads((ROOT/'contracts/response.schema.json').read_text())
        registry=Registry().with_resource(cls.req_schema['$id'],Resource.from_contents(cls.req_schema))
        cls.req_validator=Draft202012Validator(cls.req_schema,format_checker=FormatChecker())
        cls.res_validator=Draft202012Validator(cls.res_schema,registry=registry,format_checker=FormatChecker())
        with tempfile.TemporaryDirectory() as folder:
            cls.response=run(request(),ROOT,Path(folder))

    def test_schemas_and_examples_are_valid(self):
        Draft202012Validator.check_schema(self.req_schema)
        Draft202012Validator.check_schema(self.res_schema)
        self.req_validator.validate(request())
        self.res_validator.validate(self.response)

    def test_request_rejects_numeric_money_invalid_dates_unknown_fields(self):
        cases=[('outstanding_principal',1000),('valuation_date','2022-02-30'),('annual_credit_rate','NaN'),('frequency_months',True)]
        for key,value in cases:
            data=request(); data['deal'][key]=value
            self.assertTrue(list(self.req_validator.iter_errors(data)),key)
        data=request(); data['deal']['unknown']=1
        self.assertTrue(list(self.req_validator.iter_errors(data)))

    def test_response_rejects_missing_money_and_numeric_money(self):
        for mutate in ('missing','number'):
            data=deepcopy(self.response)
            row=data['results']['ANN']['payments'][0]
            if mutate=='missing': del row['interest']
            else: row['interest']=16482.09
            self.assertTrue(list(self.res_validator.iter_errors(data)))


if __name__=='__main__':
    unittest.main()
