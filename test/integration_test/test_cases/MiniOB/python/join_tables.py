import __init__

from test_case import TestCase
from test_instruction import ResultString


def create_test_cases() -> TestCase:
  join_test = TestCase()
  join_test.name = 'join-tables'

  init_group = join_test.add_execution_group('init data')
  init_group.add_sql_instruction(
      'CREATE TABLE j_user(id int, dept_id int, score int);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'CREATE TABLE j_dept(id int, label char(8));', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'CREATE TABLE j_limit(dept_id int, min_score int);', expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'INSERT INTO j_user VALUES (1, 10, 80), (2, 20, 55), (3, 10, 40), (4, 30, 90);',
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      "INSERT INTO j_dept VALUES (10, 'dev'), (20, 'qa'), (40, 'ops');",
      expected=ResultString.SUCCESS)
  init_group.add_sql_instruction(
      'INSERT INTO j_limit VALUES (10, 50), (20, 60), (40, 70);', expected=ResultString.SUCCESS)

  basic_group = join_test.add_execution_group('inner join and output names', [init_group])
  basic_group.add_block_sql_instruction('''
      SELECT j_user.id, j_dept.label FROM j_user INNER JOIN j_dept ON j_user.dept_id = j_dept.id ORDER BY j_user.id;
      j_user.id | j_dept.label
      1 | dev
      2 | qa
      3 | dev
  ''')
  basic_group.add_block_sql_instruction('''
      SELECT * FROM j_user INNER JOIN j_dept ON j_user.dept_id = j_dept.id WHERE j_user.id = 1;
      j_user.id | j_user.dept_id | j_user.score | j_dept.id | j_dept.label
      1 | 10 | 80 | 10 | dev
  ''')
  basic_group.add_block_sql_instruction('''
      SELECT j_user.*, j_dept.label FROM j_user INNER JOIN j_dept ON j_user.dept_id = j_dept.id WHERE j_user.id = 2;
      j_user.id | j_user.dept_id | j_user.score | j_dept.label
      2 | 20 | 55 | qa
  ''')

  conditions_group = join_test.add_execution_group('on and where conditions', [init_group])
  conditions_group.add_block_sql_instruction('''
      SELECT j_user.id, j_dept.label FROM j_user INNER JOIN j_dept ON j_user.dept_id = j_dept.id AND j_user.score >= 50 WHERE j_dept.id >= 10 ORDER BY j_user.id DESC;
      j_user.id | j_dept.label
      2 | qa
      1 | dev
  ''')
  conditions_group.add_block_sql_instruction('''
      SELECT j_user.id, j_dept.label FROM j_user INNER JOIN j_dept ON j_user.dept_id = j_dept.id WHERE j_user.score > 100;
      j_user.id | j_dept.label
  ''')

  chained_group = join_test.add_execution_group('chained joins', [init_group])
  chained_group.add_block_sql_instruction('''
      SELECT j_user.id, j_dept.label, j_limit.min_score FROM j_user INNER JOIN j_dept ON j_user.dept_id = j_dept.id INNER JOIN j_limit ON j_dept.id = j_limit.dept_id AND j_user.score >= j_limit.min_score ORDER BY j_user.id;
      j_user.id | j_dept.label | j_limit.min_score
      1 | dev | 50
  ''')
  chained_group.add_block_sql_instruction('''
      SELECT j_user.id, j_dept.id FROM j_user INNER JOIN j_dept ON 1 = 1 WHERE j_user.id = 1 ORDER BY j_dept.id;
      j_user.id | j_dept.id
      1 | 10
      1 | 20
      1 | 40
  ''')

  comma_group = join_test.add_execution_group('comma join compatibility', [init_group])
  comma_group.add_block_sql_instruction('''
      SELECT j_user.id, j_dept.label FROM j_user, j_dept WHERE j_user.dept_id = j_dept.id ORDER BY j_user.id;
      j_user.id | j_dept.label
      1 | dev
      2 | qa
      3 | dev
  ''')

  large_group = join_test.add_execution_group('six-table join condition pushdown')
  large_group.add_sql_instruction('CREATE TABLE jl1(id int);', expected=ResultString.SUCCESS)
  large_group.add_sql_instruction('CREATE TABLE jl2(id int);', expected=ResultString.SUCCESS)
  large_group.add_sql_instruction('CREATE TABLE jl3(id int, num3 int);', expected=ResultString.SUCCESS)
  large_group.add_sql_instruction('CREATE TABLE jl4(id int);', expected=ResultString.SUCCESS)
  large_group.add_sql_instruction('CREATE TABLE jl5(id int, num5 int);', expected=ResultString.SUCCESS)
  large_group.add_sql_instruction('CREATE TABLE jl6(id int);', expected=ResultString.SUCCESS)
  ids = ', '.join(f'({i})' for i in range(1, 13))
  large_group.add_sql_instruction(f'INSERT INTO jl1 VALUES {ids};', expected=ResultString.SUCCESS)
  large_group.add_sql_instruction(f'INSERT INTO jl2 VALUES {ids};', expected=ResultString.SUCCESS)
  large_group.add_sql_instruction(
      'INSERT INTO jl3 VALUES ' + ', '.join(f'({i}, {i})' for i in range(1, 13)) + ';',
      expected=ResultString.SUCCESS)
  large_group.add_sql_instruction(f'INSERT INTO jl4 VALUES {ids};', expected=ResultString.SUCCESS)
  large_group.add_sql_instruction(
      'INSERT INTO jl5 VALUES (1, 100), (2, 95), ' +
      ', '.join(f'({i}, 90)' for i in range(3, 13)) + ';',
      expected=ResultString.SUCCESS)
  large_group.add_sql_instruction(f'INSERT INTO jl6 VALUES {ids};', expected=ResultString.SUCCESS)
  large_group.add_block_sql_instruction('''
      SELECT count(*) FROM jl1 INNER JOIN jl2 ON jl1.id = jl2.id INNER JOIN jl3 ON jl1.id = jl3.id INNER JOIN jl4 ON jl3.id = jl4.id INNER JOIN jl5 ON 1 = 1 INNER JOIN jl6 ON jl5.id = jl6.id WHERE jl3.num3 < 10 AND jl5.num5 > 90;
      count(*)
      18
  ''')

  validation_group = join_test.add_execution_group('invalid join references', [init_group])
  validation_group.add_sql_instruction(
      'SELECT * FROM j_user INNER JOIN missing_table ON j_user.id = missing_table.id;',
      expected=ResultString.FAILURE)
  validation_group.add_sql_instruction(
      'SELECT * FROM j_user INNER JOIN j_dept ON j_user.missing = j_dept.id;',
      expected=ResultString.FAILURE)

  return join_test
