import __init__

from test_case import TestCase
from test_instruction import ResultString


def create_test_cases() -> TestCase:
  group_by_test = TestCase()
  group_by_test.name = 'group-by'

  init_group = group_by_test.add_execution_group('init data')
  init_group.add_sql_instruction(
      'CREATE TABLE group_table(id int, dept int, name char(4), score int, birthday date);',
      expected=ResultString.SUCCESS)
  rows = [
      "(1, 10, 'a', 80, '2020-01-01')",
      "(2, 10, 'b', 90, '2020-01-02')",
      "(3, 20, 'c', 70, '2021-02-03')",
      "(4, 20, 'd', 60, '2021-02-03')",
      "(5, 30, 'e', 100, '2022-03-04')",
  ]
  for row in rows:
    init_group.add_sql_instruction(f'INSERT INTO group_table VALUES {row};', expected=ResultString.SUCCESS)

  aggregate_group = group_by_test.add_execution_group('aggregate by one column', [init_group])
  aggregate_group.add_sort_block_sql_instruction('''
      SELECT dept, count(*), sum(score), avg(score), min(score), max(score) FROM group_table GROUP BY dept;
      dept | count(*) | sum(score) | avg(score) | min(score) | max(score)
      10 | 2 | 170 | 85 | 80 | 90
      20 | 2 | 130 | 65 | 60 | 70
      30 | 1 | 100 | 100 | 100 | 100
  ''')

  multi_column_group = group_by_test.add_execution_group('multiple keys and date', [init_group])
  multi_column_group.add_sort_block_sql_instruction('''
      SELECT dept, birthday, count(*), avg(score) FROM group_table GROUP BY dept, birthday;
      dept | birthday | count(*) | avg(score)
      10 | 2020-01-01 | 1 | 80
      10 | 2020-01-02 | 1 | 90
      20 | 2021-02-03 | 2 | 65
      30 | 2022-03-04 | 1 | 100
  ''')

  filter_group = group_by_test.add_execution_group('filter before grouping', [init_group])
  filter_group.add_sort_block_sql_instruction('''
      SELECT dept, count(*) FROM group_table WHERE score >= 80 GROUP BY dept;
      dept | count(*)
      10 | 2
      30 | 1
  ''')

  expression_group = group_by_test.add_execution_group('group expression', [init_group])
  expression_group.add_sort_block_sql_instruction('''
      SELECT dept + 1, count(*) FROM group_table GROUP BY dept + 1;
      dept + 1 | count(*)
      11 | 2
      21 | 2
      31 | 1
  ''')

  validation_group = group_by_test.add_execution_group('group validation', [init_group])
  validation_group.add_sql_instruction(
      'SELECT id, count(*) FROM group_table GROUP BY dept;', expected=ResultString.FAILURE)

  join_group = group_by_test.add_execution_group('multi-table grouping', [init_group])
  join_group.add_sql_instruction('CREATE TABLE dept_table(dept int, budget int);', expected=ResultString.SUCCESS)
  join_group.add_sql_instruction('INSERT INTO dept_table VALUES (10, 1000);', expected=ResultString.SUCCESS)
  join_group.add_sql_instruction('INSERT INTO dept_table VALUES (20, 2000);', expected=ResultString.SUCCESS)
  join_group.add_sql_instruction('INSERT INTO dept_table VALUES (30, 3000);', expected=ResultString.SUCCESS)
  join_group.add_sort_block_sql_instruction('''
      SELECT group_table.dept, avg(group_table.score), max(dept_table.budget) FROM group_table, dept_table WHERE group_table.dept = dept_table.dept GROUP BY group_table.dept;
      dept | avg(group_table.score) | max(dept_table.budget)
      10 | 85 | 1000
      20 | 65 | 2000
      30 | 100 | 3000
  ''')

  empty_group = group_by_test.add_execution_group('empty input', [init_group])
  empty_group.add_sql_instruction('CREATE TABLE empty_group(id int, dept int);', expected=ResultString.SUCCESS)
  empty_group.add_block_sql_instruction('''
      SELECT dept, count(*) FROM empty_group GROUP BY dept;
      dept | count(*)
  ''')

  return group_by_test
