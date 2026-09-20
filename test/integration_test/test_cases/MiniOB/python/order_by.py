import __init__

from test_case import TestCase
from test_instruction import ResultString


def create_test_cases() -> TestCase:
  order_by_test = TestCase()
  order_by_test.name = 'order-by'

  init_group = order_by_test.add_execution_group('init data')
  init_group.add_sql_instruction(
      'CREATE TABLE order_table(id int, dept int, name char(6), score float, birthday date);',
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      "INSERT INTO order_table VALUES "
      "(1, 20, 'beta', 80.5, '2022-01-03'), "
      "(2, 10, 'alfa', 90, '2020-02-01'), "
      "(3, 20, 'char', 70, '2022-01-01'), "
      "(4, 10, 'delta', 90, '2021-06-15'), "
      "(5, 30, 'echo', 60, '2019-12-31');",
      expected=ResultString.SUCCESS)

  basic_group = order_by_test.add_execution_group('default ascending and descending', [init_group])
  basic_group.add_block_sql_instruction('''
      SELECT id, name FROM order_table ORDER BY id;
      id | name
      1 | beta
      2 | alfa
      3 | char
      4 | delta
      5 | echo
  ''')
  basic_group.add_block_sql_instruction('''
      SELECT id, score FROM order_table ORDER BY score DESC, id ASC;
      id | score
      2 | 90
      4 | 90
      1 | 80.5
      3 | 70
      5 | 60
  ''')

  types_group = order_by_test.add_execution_group('strings and dates', [init_group])
  types_group.add_block_sql_instruction('''
      SELECT id, name FROM order_table ORDER BY name DESC;
      id | name
      5 | echo
      4 | delta
      3 | char
      1 | beta
      2 | alfa
  ''')
  types_group.add_block_sql_instruction('''
      SELECT id, birthday FROM order_table ORDER BY birthday ASC;
      id | birthday
      5 | 2019-12-31
      2 | 2020-02-01
      4 | 2021-06-15
      3 | 2022-01-01
      1 | 2022-01-03
  ''')

  expression_group = order_by_test.add_execution_group('filter expressions and hidden keys', [init_group])
  expression_group.add_block_sql_instruction('''
      SELECT name FROM order_table WHERE score >= 70 ORDER BY dept ASC, id DESC;
      name
      delta
      alfa
      char
      beta
  ''')
  expression_group.add_block_sql_instruction('''
      SELECT id, dept + id FROM order_table ORDER BY dept + id DESC;
      id | dept + id
      5 | 35
      3 | 23
      1 | 21
      4 | 14
      2 | 12
  ''')

  join_group = order_by_test.add_execution_group('qualified multi-table keys', [init_group])
  join_group.add_sql_instruction('CREATE TABLE dept_table(dept int, label char(6));', expected=ResultString.SUCCESS)
  join_group.add_sql_instruction(
      "INSERT INTO dept_table VALUES (10, 'low'), (20, 'middle'), (30, 'top');",
      expected=ResultString.SUCCESS)
  join_group.add_block_sql_instruction('''
      SELECT order_table.id, dept_table.label FROM order_table, dept_table WHERE order_table.dept = dept_table.dept ORDER BY dept_table.label DESC, order_table.id ASC;
      order_table.id | dept_table.label
      5 | top
      1 | middle
      3 | middle
      2 | low
      4 | low
  ''')

  aggregate_group = order_by_test.add_execution_group('grouped aggregate keys', [init_group])
  aggregate_group.add_block_sql_instruction('''
      SELECT dept, count(*) FROM order_table GROUP BY dept ORDER BY count(*) DESC, dept ASC;
      dept | count(*)
      10 | 2
      20 | 2
      30 | 1
  ''')

  validation_group = order_by_test.add_execution_group('validation and empty input', [init_group])
  validation_group.add_sql_instruction(
      'SELECT id FROM order_table ORDER BY missing;', expected=ResultString.FAILURE)
  validation_group.add_sql_instruction('CREATE TABLE empty_order(id int);', expected=ResultString.SUCCESS)
  validation_group.add_block_sql_instruction('''
      SELECT id FROM empty_order ORDER BY id DESC;
      id
  ''')

  return order_by_test
