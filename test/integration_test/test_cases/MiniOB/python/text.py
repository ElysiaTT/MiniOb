import __init__

from test_case import TestCase
from test_instruction import RestartInstruction, ResultString


def create_test_cases() -> TestCase:
  case = TestCase()
  case.name = 'text'
  group = case.add_execution_group('text boundaries and off-page storage')
  group.add_sql_instruction('CREATE TABLE texts(id int, body text, extra text NULL);',
                            expected=ResultString.SUCCESS)
  group.add_sql_instruction('CREATE UNIQUE INDEX texts_id ON texts(id);', expected=ResultString.SUCCESS)
  values = ['', 'x', 'a' * 4095, 'b' * 4096, 'c' * 4097]
  for i, value in enumerate(values):
    group.add_sql_instruction(f"INSERT INTO texts VALUES ({i}, '{value}', '{'z' * 4096}');",
                              expected=ResultString.SUCCESS)
    group.add_sql_instruction(f'SELECT id, body FROM texts WHERE id = {i};',
                              expected_messages=['id | body', f'{i} | {value[:4096]}'.strip()])
  group.add_sql_instruction('SELECT body, extra FROM texts WHERE id = 3;',
                            expected_messages=['body | extra', 'b' * 4096 + ' | ' + 'z' * 4096])
  group.add_sql_instruction("UPDATE texts SET body = 'changed' WHERE id = 1;",
                            expected=ResultString.SUCCESS)
  group.add_sql_instruction("UPDATE texts SET extra = NULL WHERE id = 1;",
                            expected=ResultString.SUCCESS)
  group.add_sql_instruction("SELECT id FROM texts WHERE body = 'changed';", expected_messages=['id', '1'])
  group.add_sql_instruction('SELECT id FROM texts WHERE extra IS NULL;', expected_messages=['id', '1'])
  group.add_sql_instruction(f"UPDATE texts SET body = '{'q' * 5000}' WHERE id = 2;",
                            expected=ResultString.SUCCESS)
  group.add_sql_instruction("DELETE FROM texts WHERE body = 'changed';", expected=ResultString.SUCCESS)
  restart = case.add_execution_group('text restart and table recreation', [group])
  restart.add_instruction(RestartInstruction())
  restart.add_sql_instruction('SELECT body FROM texts WHERE id = 2;', expected_messages=['body', 'q' * 4096])
  restart.add_sql_instruction('SELECT count(*) FROM texts;', expected_messages=['count(*)', '4'])
  restart.add_sql_instruction('DROP TABLE texts;', expected=ResultString.SUCCESS)
  restart.add_sql_instruction('CREATE TABLE texts(id int, body text);', expected=ResultString.SUCCESS)
  restart.add_sql_instruction("INSERT INTO texts VALUES (1, 'fresh');", expected=ResultString.SUCCESS)
  restart.add_sql_instruction('SELECT * FROM texts;', expected_messages=['id | body', '1 | fresh'])
  return case
