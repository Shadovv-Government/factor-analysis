"""Regression targets: calendar drift, rounding, principal leakage, rate mixing."""
import json
import unittest
from datetime import date
from decimal import Decimal
from pathlib import Path

try:
    from sprint1.model import Deal, build_schedule, discount_schedule, payment_dates
except ImportError:
    Deal = build_schedule = discount_schedule = payment_dates = None

ROOT = Path(__file__).resolve().parents[1]


def request():
    return json.loads((ROOT / 'contracts/request.example.json').read_text(encoding='utf-8'))


class ModelTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(Deal, 'Расчётный компонент ещё не реализован')
        self.deal = Deal.from_dict(request())

    def test_source_ann_first_and_final_payments(self):
        rows = build_schedule(self.deal, 'ANN')
        self.assertEqual(len(rows), 334)
        first, last = rows[0], rows[-1]
        self.assertEqual(first.interest, Decimal('16482.09'))
        self.assertEqual(first.principal, Decimal('9521.11'))
        self.assertEqual(first.payment, Decimal('26003.20'))
        self.assertEqual(last.payment, Decimal('26005.46'))
        self.assertEqual(last.closing_balance, Decimal('0.00'))

    def test_source_simple_balloon(self):
        rows = build_schedule(self.deal, 'SIMPLE')
        self.assertEqual(rows[0].principal, Decimal('0.00'))
        self.assertEqual(rows[-1].principal, Decimal('3646037.89'))
        self.assertEqual(rows[-1].payment, Decimal('3664018.35'))

    def test_source_rgm_fixed_principal(self):
        rows = build_schedule(self.deal, 'RGM')
        self.assertEqual(rows[0].principal, Decimal('10916.28'))
        self.assertEqual(rows[-1].principal, Decimal('10916.65'))
        self.assertEqual(rows[-1].interest, Decimal('53.84'))

    def test_all_schedules_conserve_money(self):
        for kind in ('ANN', 'SIMPLE', 'RGM'):
            rows = build_schedule(self.deal, kind)
            self.assertEqual(sum(r.principal for r in rows), self.deal.principal)
            self.assertEqual(rows[-1].closing_balance, 0)
            for row in rows:
                self.assertEqual(row.payment, row.interest + row.principal)
                self.assertEqual(row.closing_balance, row.opening_balance - row.principal)
                self.assertGreaterEqual(row.closing_balance, 0)

    def test_manual_one_period_interest(self):
        data = request()
        data['deal'].update(valuation_date='2023-01-01', first_payment_date='2023-01-31', maturity_date='2023-01-31', outstanding_principal='1000.00', annual_credit_rate='0.12')
        row = build_schedule(Deal.from_dict(data), 'SIMPLE')[0]
        self.assertEqual(row.days, 30)
        self.assertEqual(row.interest, Decimal('9.86'))  # 1000*0.12*30/365
        self.assertEqual(row.payment, Decimal('1009.86'))

    def test_leap_year_uses_fixed_365(self):
        data = request()
        data['deal'].update(valuation_date='2024-02-01', first_payment_date='2024-03-01', maturity_date='2024-03-01', outstanding_principal='1000.00', annual_credit_rate='0.12')
        row = build_schedule(Deal.from_dict(data), 'SIMPLE')[0]
        self.assertEqual(row.days, 29)
        self.assertEqual(row.interest, Decimal('9.53'))

    def test_calendar_end_of_month_without_drift(self):
        data = request()
        data['deal'].update(valuation_date='2023-01-01', first_payment_date='2023-01-31', maturity_date='2023-04-15')
        self.assertEqual(payment_dates(Deal.from_dict(data)), [date(2023,1,31), date(2023,2,28), date(2023,3,31), date(2023,4,15)])

    def test_zero_credit_and_discount_rates(self):
        data = request()
        data['deal']['annual_credit_rate'] = '0'
        data['discounting']['annual_rate'] = '0'
        deal = Deal.from_dict(data)
        for kind in ('ANN', 'SIMPLE', 'RGM'):
            rows = build_schedule(deal, kind)
            self.assertEqual(sum(r.interest for r in rows), 0)
            self.assertEqual(sum(r['pv_payment'] for r in discount_schedule(deal, rows)), deal.principal)

    def test_half_up_and_tiny_principal_no_negative_balance(self):
        data = request()
        data['deal'].update(valuation_date='2023-01-01', first_payment_date='2023-01-02', maturity_date='2023-01-02', outstanding_principal='1.00', annual_credit_rate='1.825')
        self.assertEqual(build_schedule(Deal.from_dict(data), 'ANN')[0].interest, Decimal('0.01'))
        data = request()
        data['deal']['outstanding_principal'] = '0.01'
        for kind in ('ANN', 'RGM'):
            rows = build_schedule(Deal.from_dict(data), kind)
            self.assertEqual(sum(r.principal for r in rows), Decimal('0.01'))
            self.assertTrue(all(r.closing_balance >= 0 for r in rows))

    def test_independent_discount_rate_changes_pv_only(self):
        rows = build_schedule(self.deal, 'ANN')
        low = sum(x['pv_payment'] for x in discount_schedule(self.deal, rows, Decimal('0.05')))
        base = sum(x['pv_payment'] for x in discount_schedule(self.deal, rows))
        high = sum(x['pv_payment'] for x in discount_schedule(self.deal, rows, Decimal('0.10')))
        self.assertGreater(low, base)
        self.assertGreater(base, high)
        self.assertLess(abs(base - self.deal.principal), Decimal('0.04'))
        self.assertEqual(rows[0].payment, Decimal('26003.20'))

    def test_reject_invalid_contract(self):
        cases = [
            ('outstanding_principal', '-1.00'), ('outstanding_principal','0.00'),
            ('outstanding_principal','1.001'), ('outstanding_principal',100),
            ('annual_credit_rate','NaN'), ('annual_credit_rate','Infinity'),
            ('annual_credit_rate','-0.01'), ('frequency_months',0),
            ('frequency_months',True), ('frequency_months',1.5),
            ('valuation_date','2022-8-31'), ('valuation_date','2022-09-22'),
            ('first_payment_date','2051-01-01'), ('day_count','ACT/360')
        ]
        for key, value in cases:
            with self.subTest(key=key,value=value):
                data=request(); data['deal'][key]=value
                with self.assertRaises(ValueError): Deal.from_dict(data)
        data=request(); data['surprise']=1
        with self.assertRaises(ValueError): Deal.from_dict(data)
        with self.assertRaises(ValueError): build_schedule(self.deal, 'UNKNOWN')

    def test_reject_invalid_discount_rate(self):
        rows=build_schedule(self.deal,'ANN')
        for value in (Decimal('-0.01'),Decimal('NaN'),Decimal('Infinity')):
            with self.assertRaises(ValueError): discount_schedule(self.deal,rows,value)


if __name__ == '__main__':
    unittest.main()
