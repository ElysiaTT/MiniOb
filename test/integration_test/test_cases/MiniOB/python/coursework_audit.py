import __init__
from test_case import TestCase
from test_instruction import RestartInstruction, ResultString


def create_test_cases() -> TestCase:
  case = TestCase()
  case.name = 'coursework-audit'
  g = case.add_execution_group('mandatory tasks and cross-feature boundaries')
  def sql(query, expected=ResultString.SUCCESS):
    g.add_sql_instruction(query, expected_messages=expected.splitlines())
  sql('CREATE TABLE audit(id int, age int, birthday date);')
  sql("INSERT INTO audit VALUES (1, 10, '2020-2-29'), (2, 20, '2021-1-2');")
  sql('CREATE UNIQUE INDEX audit_id ON audit(id);')
  sql('CREATE INDEX audit_date ON audit(birthday);')
  sql('UPDATE audit SET age = 99, id = 4 WHERE id = 1;', ResultString.FAILURE)
  sql('SELECT age FROM audit WHERE id = 1;', 'age\n10')
  for query in ['SELECT missing FROM audit;', 'SELECT * FROM absent;',
                'SELECT * FROM audit WHERE missing = 1;',
                'UPDATE audit SET missing = 1;', 'UPDATE absent SET id = 1;',
                "UPDATE audit SET age = 'bad' WHERE id = 1;",
                "INSERT INTO audit VALUES (3, 30, '2021-2-29');",
                "UPDATE audit SET birthday = '2020-13-01';",
                "SELECT * FROM audit WHERE birthday = '2021-2-30';"]:
    sql(query, ResultString.FAILURE)
  sql('SELECT *, id FROM audit WHERE id = 1;', 'id | age | birthday | id\n1 | 10 | 2020-02-29 | 1')
  sql('SELECT id FROM audit WHERE id = NULL;', 'id')
  sql('SELECT id/0, NULL+id FROM audit WHERE id = 1;', 'id/0 | NULL+id\nNULL | NULL')
  sql('SELECT id FROM audit WHERE id/0 > 1;', 'id')
  sql('UPDATE audit SET age = 77 WHERE id = 999;')
  sql("UPDATE audit SET age = 11 WHERE id = 1 AND birthday = '2020-02-29';")
  sql('SELECT age FROM audit WHERE id = 1;', 'age\n11')
  sql('UPDATE audit SET age = 30;')
  sql('UPDATE audit SET id = 3 WHERE id = 2;')
  sql('SELECT id FROM audit WHERE id = 2;', 'id')
  sql('SELECT id FROM audit WHERE id = 3;', 'id\n3')
  sql("SELECT id FROM audit WHERE birthday = '2020-02-29';", 'id\n1')
  sql('CREATE TABLE other(id int);')
  sql('INSERT INTO other VALUES (1), (3);')
  sql('SELECT audit.id, other.id FROM audit, other WHERE audit.id = other.id ORDER BY audit.id;',
      'audit.id | other.id\n1 | 1\n3 | 3')
  sql('SELECT audit.*, other.* FROM audit, other WHERE audit.id = other.id ORDER BY audit.id;',
      'audit.id | audit.age | audit.birthday | other.id\n'
      '1 | 30 | 2020-02-29 | 1\n3 | 30 | 2021-01-02 | 3')
  sql('CREATE TABLE third(id int);')
  sql('INSERT INTO third VALUES (1), (4);')
  sql('SELECT audit.id, other.id, third.id FROM audit, other, third '
      'WHERE audit.id = other.id AND other.id = third.id;',
      'audit.id | other.id | third.id\n1 | 1 | 1')
  sql('SELECT count(*), count(1), count(id), min(age), max(age), avg(age) FROM audit;',
      'count(*) | count(1) | count(id) | min(age) | max(age) | avg(age)\n'
      '2 | 2 | 2 | 30 | 30 | 30')
  sql('DELETE FROM other WHERE id = 3;')
  sql('SELECT id FROM other ORDER BY id;', 'id\n1')
  g.add_instruction(RestartInstruction())
  sql('SELECT id FROM audit ORDER BY id;', 'id\n1\n3')
  sql('DROP TABLE audit;')
  sql('SELECT * FROM audit;', ResultString.FAILURE)
  sql('CREATE TABLE audit(id int, name char(8));')
  sql("INSERT INTO audit VALUES (1, 'new');")
  sql('SELECT * FROM audit;', 'id | name\n1 | new')
  return case
