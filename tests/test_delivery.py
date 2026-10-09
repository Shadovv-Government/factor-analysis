"""Source reconciliation must catch stale values, changed dates and amounts."""
import json
import subprocess
import sys
import tempfile
import unittest
from copy import deepcopy
from decimal import Decimal
from pathlib import Path

from sprint1.model import Deal, build_schedule, discount_schedule
from test_model import ROOT, request

try:
    from sprint1.run import reconcile_sources, run
except ImportError:
    reconcile_sources = run = None


class DeliveryTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(reconcile_sources, 'Сверка и экспорт ещё не реализованы')
        self.data=request()
        self.deal=Deal.from_dict(self.data)
        self.schedules={k:discount_schedule(self.deal,build_schedule(self.deal,k)) for k in self.deal.payment_types}

    def test_every_original_source_row_agrees(self):
        result=reconcile_sources(ROOT,self.deal,self.schedules)
        self.assertTrue(result['passed'],result.get('mismatches'))
        self.assertEqual(result['source_ann_rows'],334)
        self.assertEqual(result['discounted_rows'],1002)
        self.assertGreater(result['comparisons'],10000)

    def test_reconciliation_detects_money_change(self):
        bad=deepcopy(self.schedules)
        bad['ANN'][150]['interest']+=Decimal('0.01')
        result=reconcile_sources(ROOT,self.deal,bad)
        self.assertFalse(result['passed'])
        self.assertGreater(result['mismatch_count'],0)

    def test_reconciliation_detects_date_change(self):
        bad=deepcopy(self.schedules)
        bad['RGM'][10]['date']='2023-08-23'
        self.assertFalse(reconcile_sources(ROOT,self.deal,bad)['passed'])

    def test_output_files_and_totals(self):
        with tempfile.TemporaryDirectory() as folder:
            payload=run(self.data,ROOT,Path(folder),verify_references=True)
            self.assertEqual(set(payload['results']),{'ANN','SIMPLE','RGM'})
            for k in payload['results']:
                summary=payload['results'][k]['summary']
                self.assertEqual(summary['total_principal'],'3646037.89')
                self.assertEqual(summary['closing_balance'],'0.00')
                self.assertEqual(len(payload['results'][k]['payments']),334)
                self.assertTrue((Path(folder)/f'{k}.csv').is_file())
            self.assertTrue((Path(folder)/'report.md').is_file())
            self.assertTrue((Path(folder)/'verification.json').is_file())
            parsed=json.loads((Path(folder)/'calculation.json').read_text(encoding='utf-8'))
            self.assertEqual(parsed,payload)

    def test_cli_bad_input_is_nonzero_and_does_not_write_success(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'bad.json'
            bad=request(); bad['deal']['outstanding_principal']='-1.00'
            p.write_text(json.dumps(bad),encoding='utf-8')
            out=Path(folder)/'output'
            result=subprocess.run([sys.executable,'-m','sprint1.run','--input',str(p),'--output',str(out)],cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
            self.assertNotEqual(result.returncode,0)
            self.assertFalse((out/'calculation.json').exists())

    def test_other_request_report_uses_its_own_amounts(self):
        data=request()
        data['deal'].update(outstanding_principal='1000.00', maturity_date='2022-11-22')
        with tempfile.TemporaryDirectory() as folder:
            run(data,ROOT,Path(folder),verify_references=False)
            text=(Path(folder)/'report.md').read_text(encoding='utf-8')
            self.assertIn('1 000,00',text)
            self.assertNotIn('3 646 037,89',text)

    def test_valid_zero_rate_deal_survives_unsupported_auxiliary_case(self):
        data=request()
        data['deal'].update(outstanding_principal='1000.00', valuation_date='2023-01-01', first_payment_date='2024-01-01', maturity_date='2034-01-01', annual_credit_rate='0')
        data['discounting']['annual_rate']='0'
        with tempfile.TemporaryDirectory() as folder:
            payload=run(data,ROOT,Path(folder),verify_references=False)
            self.assertEqual(payload['results']['ANN']['summary']['total_principal'],'1000.00')
            self.assertTrue((Path(folder)/'report.md').exists())
            cases=json.loads((Path(folder)/'validation_cases.json').read_text())
            credit_case=next(c for c in cases if c['name']=='credit_10')
            self.assertTrue(credit_case['results']['ANN']['unsupported'])

    def test_maximum_principal_does_not_fail_due_to_double_auxiliary_case(self):
        data=request(); data['deal']['outstanding_principal']='999999999999999.99'
        data['deal']['annual_credit_rate']='0'; data['discounting']['annual_rate']='0'
        with tempfile.TemporaryDirectory() as folder:
            run(data,ROOT,Path(folder),verify_references=False)
            cases=json.loads((Path(folder)/'validation_cases.json').read_text())
            doubled=next(c for c in cases if c['name']=='principal_double')
            self.assertEqual(doubled['status'],'not_applicable')
            self.assertTrue((Path(folder)/'RGM.csv').exists())


if __name__ == '__main__':
    unittest.main()
