"""Contractual cash flows; nominal ACT/365F and cent rounding with HALF_UP.

The borrowing rate determines payments. The independent discount rate only
determines their present value. Neither valuation is Economic Capital.
"""
from calendar import monthrange
from dataclasses import dataclass
from datetime import date
from decimal import Decimal, ROUND_HALF_UP, localcontext
import re

CENT = Decimal('0.01')
ZERO = Decimal('0.00')
MAX_PAYMENTS = 1200


def money(value: Decimal) -> Decimal:
    return value.quantize(CENT, rounding=ROUND_HALF_UP)


def _object(value, fields, label):
    if not isinstance(value, dict) or set(value) != set(fields):
        raise ValueError(f'{label}: expected fields {sorted(fields)}')


def _decimal(value, label, *, monetary=False):
    pattern = r'(0|[1-9][0-9]{0,14})\.[0-9]{2}' if monetary else r'(0|[1-9][0-9]*)(\.[0-9]+)?'
    if not isinstance(value, str) or len(value) > 32 or re.fullmatch(pattern, value) is None:
        raise ValueError(f'{label}: expected nonnegative decimal string')
    result = Decimal(value)
    if not result.is_finite() or result < 0:
        raise ValueError(f'{label}: expected finite nonnegative decimal')
    return result


def _date(value, label):
    if not isinstance(value, str) or re.fullmatch(r'[0-9]{4}-[0-9]{2}-[0-9]{2}', value) is None:
        raise ValueError(f'{label}: expected YYYY-MM-DD')
    try:
        return date.fromisoformat(value)
    except ValueError as exc:
        raise ValueError(f'{label}: invalid calendar date') from exc


@dataclass(frozen=True)
class Deal:
    id: str
    currency: str
    valuation_date: date
    principal: Decimal
    credit_rate: Decimal
    first_payment_date: date
    maturity_date: date
    frequency_months: int
    discount_rate: Decimal
    payment_types: tuple[str, ...]

    @classmethod
    def from_dict(cls, data: dict) -> 'Deal':
        _object(data, ('schema_version','deal','discounting','payment_types'), 'request')
        if data['schema_version'] != '1.0':
            raise ValueError('schema_version: expected 1.0')
        fields=('id','currency','valuation_date','outstanding_principal','annual_credit_rate','first_payment_date','maturity_date','frequency_months','day_count')
        source=data['deal']; disc=data['discounting']
        _object(source, fields, 'deal')
        _object(disc, ('annual_rate','method','day_count'), 'discounting')
        if source['day_count'] != 'ACT/365F' or disc['day_count'] != 'ACT/365F' or disc['method'] != 'PERIOD_SIMPLE_CHAIN':
            raise ValueError('Only ACT/365F and PERIOD_SIMPLE_CHAIN are supported')
        if not isinstance(source['id'], str) or not 1 <= len(source['id']) <= 128:
            raise ValueError('id: expected nonempty string of at most 128 characters')
        if source['currency'] != 'RUB':
            raise ValueError('currency: version 1 supports RUB only')
        frequency=source['frequency_months']
        if type(frequency) is not int or not 1 <= frequency <= 120:
            raise ValueError('frequency_months: expected integer from 1 to 120')
        principal=_decimal(source['outstanding_principal'],'outstanding_principal',monetary=True)
        if principal <= 0:
            raise ValueError('outstanding_principal must be positive')
        valuation=_date(source['valuation_date'],'valuation_date')
        first=_date(source['first_payment_date'],'first_payment_date')
        maturity=_date(source['maturity_date'],'maturity_date')
        if not valuation < first <= maturity:
            raise ValueError('Expected valuation_date < first_payment_date <= maturity_date')
        kinds=data['payment_types']
        if not isinstance(kinds,list) or not 1 <= len(kinds) <= 3 or any(not isinstance(k,str) or k not in ('ANN','SIMPLE','RGM') for k in kinds) or len(set(kinds)) != len(kinds):
            raise ValueError('payment_types: nonempty unique subset of ANN, SIMPLE, RGM')
        result=cls(source['id'],source['currency'],valuation,principal,
            _decimal(source['annual_credit_rate'],'annual_credit_rate'), first,maturity,frequency,
            _decimal(disc['annual_rate'],'annual_rate'),tuple(kinds))
        payment_dates(result)  # enforce the bounded prototype horizon
        return result


def payment_dates(deal: Deal) -> list[date]:
    """Anchor every month to the original first date; include maturity once."""
    result=[]
    first=deal.first_payment_date
    maturity_month=deal.maturity_date.year*12+deal.maturity_date.month-1
    for i in range(MAX_PAYMENTS):
        absolute=first.year*12+first.month-1+i*deal.frequency_months
        if absolute > maturity_month:
            result.append(deal.maturity_date)
            return result
        year, month=divmod(absolute,12); month+=1
        candidate=date(year,month,min(first.day,monthrange(year,month)[1]))
        if candidate >= deal.maturity_date:
            result.append(deal.maturity_date)
            return result
        result.append(candidate)
    raise ValueError(f'At most {MAX_PAYMENTS} payments are supported')


@dataclass(frozen=True)
class Payment:
    number: int
    date: date
    days: int
    opening_balance: Decimal
    interest: Decimal
    principal: Decimal
    payment: Decimal
    closing_balance: Decimal
    credit_period_rate: Decimal


def build_schedule(deal: Deal, kind: str) -> list[Payment]:
    if kind not in ('ANN','SIMPLE','RGM'):
        raise ValueError('Unknown payment type')
    with localcontext() as ctx:
        ctx.prec=40
        dates=payment_dates(deal)
        previous=deal.valuation_date
        periods=[]
        credit_df=Decimal(1)
        sum_credit_df=Decimal(0)
        for current in dates:
            days=(current-previous).days
            period_rate=deal.credit_rate*days/Decimal(365)
            credit_df /= 1+period_rate
            sum_credit_df += credit_df
            periods.append((current,days,period_rate))
            previous=current
        regular=money(deal.principal/sum_credit_df)
        regular_principal=money(deal.principal/len(dates))
        balance=deal.principal
        rows=[]
        for i,(current,days,rate) in enumerate(periods,1):
            interest=money(balance*rate)
            if i==len(dates):
                principal=balance
            elif kind=='SIMPLE':
                principal=ZERO
            elif kind=='RGM':
                principal=min(balance,regular_principal)
            else:
                principal=money(min(regular,balance+interest)-interest)
                if principal < 0:
                    raise ValueError('ANN payment is below interest: negative amortization is not supported')
            payment=money(interest+principal)
            closing=money(balance-principal)
            rows.append(Payment(i,current,days,balance,interest,principal,payment,closing,rate))
            balance=closing
        return rows


def discount_schedule(deal: Deal, rows: list[Payment], rate: Decimal | None=None) -> list[dict]:
    rate=deal.discount_rate if rate is None else rate
    if not isinstance(rate,Decimal) or not rate.is_finite() or rate < 0:
        raise ValueError('Discount rate must be a finite nonnegative Decimal')
    with localcontext() as ctx:
        ctx.prec=40
        df=Decimal(1)
        result=[]
        previous=deal.valuation_date
        for row in rows:
            days=(row.date-previous).days
            if days <= 0 or row.days != days:
                raise ValueError('Cash-flow dates must increase strictly and agree with day counts')
            discount_period_rate=rate*days/Decimal(365)
            df /= 1+discount_period_rate
            result.append({
                'number':row.number,'date':row.date.isoformat(),'days':row.days,
                'opening_balance':row.opening_balance,'interest':row.interest,
                'principal':row.principal,'payment':row.payment,'closing_balance':row.closing_balance,
                'credit_period_rate':row.credit_period_rate,
                'discount_period_rate':discount_period_rate,'discount_factor':df,
                'pv_interest':row.interest*df,'pv_principal':row.principal*df,
                'pv_payment':row.payment*df
            })
            previous=row.date
        return result


def summarize(deal: Deal, valued: list[dict]) -> dict:
    with localcontext() as ctx:
        ctx.prec=40
        sums={k:sum((r[k] for r in valued),Decimal(0)) for k in ('interest','principal','payment','pv_interest','pv_principal','pv_payment')}
        return {
            'payment_count':len(valued),
            'total_interest':sums['interest'],'total_principal':sums['principal'],
            'total_payments':sums['payment'],'pv_interest':sums['pv_interest'],
            'pv_principal':sums['pv_principal'],'pv_future_cashflows':sums['pv_payment'],
            'pv_minus_outstanding':sums['pv_payment']-deal.principal,
            'first_payment':valued[0]['payment'],'last_payment':valued[-1]['payment'],
            'closing_balance':valued[-1]['closing_balance']
        }
