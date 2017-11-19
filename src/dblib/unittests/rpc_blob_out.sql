CREATE PROCEDURE #rpc_blob_out
  @txt varchar(max) OUTPUT
AS
BEGIN
declare @i int
select @i=6000, @txt=''
while @i>0
	select @i=@i-1, @txt=@txt+'pad-len-10'
set @txt=@txt+'test-OK'
return 0
END

go
IF OBJECT_ID('rpc_blob_out') IS NOT NULL DROP PROC rpc_blob_out
go
CREATE PROCEDURE rpc_blob_out
  @txt varchar(max) OUTPUT
AS
BEGIN
declare @i int
select @i=6000, @txt=''
while @i>0
	select @i=@i-1, @txt=@txt+'pad-len-10'
set @txt=@txt+'test-OK'
return 0
END

go
IF OBJECT_ID('rpc_blob_out') IS NOT NULL DROP PROC rpc_blob_out
go
